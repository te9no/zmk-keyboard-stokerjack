"""Backup-first Stokerjack RMK installer. No mass erase or automatic recovery."""
from __future__ import annotations
import argparse, hashlib, json, os, re, secrets, struct, subprocess, sys, tempfile, time
from datetime import datetime, timezone
from pathlib import Path

START, END, STORAGE_END, SETTINGS = 0x1000, 0xEC000, 0xF4000, 0xFF000
ROM_ID = b'STOKERJACK-RMK-OST60-v001\0'
VIAL_ID = b'STOKRMK\x01'

def sha(data): return hashlib.sha256(data).hexdigest()
def save_json(path, value): path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
def need(ok, message):
    if not ok: raise RuntimeError(message)

def bundle(path):
    path = Path(path).resolve()
    m = json.loads((path/'manifest.json').read_text(encoding='utf-8'))
    need(m['target'] == 'Stokerjack-OST60-nRF52840' and m['start'] == START, '対象ファームが違います')
    data = (path/'stokerjack-rmk.bin').read_bytes()
    need(16 < len(data) <= END-START and sha(data) == m['bin_sha256'], 'ファームのサイズ／SHA256が一致しません')
    sp, pc = struct.unpack_from('<II', data)
    need(0x20000008 <= sp <= 0x20040000 and START <= (pc & ~1) < START+len(data) and pc & 1, 'ベクタが不正です')
    need(ROM_ID in data, 'OST60ファーム識別子がありません')
    need(0x20000008 <= m['telemetry'] <= 0x20040000-64 and m['telemetry'] % 4 == 0, '診断アドレスが不正です')
    return m, data

def tcl_path(path):
    p = str(Path(path).resolve()).replace('\\','/')
    need(not any(c in p for c in '{}\r\n'), 'パスに使用できない文字があります')
    return '{'+p+'}'

class OpenOCD:
    def __init__(self, exe, scripts, serial='', log=lambda s: None):
        self.exe, self.scripts, self.serial, self.log = str(exe), str(scripts), serial, log
        need(not serial or re.fullmatch(r'[A-Za-z0-9_-]+', serial), 'ST-Linkシリアルが不正です')
        self.identity = None

    def run(self, commands):
        cfg = ['source [find interface/stlink.cfg]', 'transport select swd',
               'source [find target/nordic/nrf52.cfg]', 'adapter speed 100',
               'gdb port disabled', 'tcl port disabled', 'telnet port disabled']
        if self.serial: cfg.append('adapter serial '+self.serial)
        cfg.append('init')
        cfg.append('if {[lindex [read_memory 0x10000100 32 1] 0] != 0x52840} {error "Not nRF52840"}')
        if self.identity:
            lo, hi = struct.unpack('<II', self.identity)
            cfg.extend([f'if {{[lindex [read_memory {a:#x} 32 1] 0] != {v}}} {{error "Device changed"}}'
                        for a,v in [(0x10000060,lo),(0x10000064,hi)]])
        cfg.extend(commands)
        cfg.append('shutdown')
        with tempfile.TemporaryDirectory(prefix='stokerjack-') as tmp:
            script = Path(tmp)/'session.cfg'; script.write_text('\n'.join(cfg), encoding='utf-8')
            result = subprocess.run([self.exe,'-s',self.scripts,'-f',str(script)],
                capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        output = result.stdout+result.stderr
        self.log(output)
        need(result.returncode == 0 and not re.search(r'^Error:', output, re.M),
             'OpenOCD失敗。接続・電源・SWD配線を確認してください。詳細はログに記録しました')

    def read(self, address, size, halt=False):
        with tempfile.TemporaryDirectory(prefix='stokerjack-read-') as tmp:
            p = Path(tmp)/'read.bin'
            self.run((['reset halt'] if halt else [])+[f'dump_image {tcl_path(p)} {address:#x} {size}'])
            data = p.read_bytes(); need(len(data) == size, '読み出しサイズが不正です'); return data

    def identify(self):
        info = self.read(0x10000100, 20)
        part, variant, package, ram, flash = struct.unpack('<5I',info)
        need(part == 0x52840 and ram == 256 and flash == 1024, 'nRF52840 1MB/256KBではありません')
        self.identity = self.read(0x10000060, 8)
        return self.identity.hex()

    def write_app(self, binary, settings, migrate):
        commands = ['reset halt']
        if migrate: commands.append(f'flash erase_address {END:#x} {STORAGE_END-END}')
        commands += [f'flash write_image erase {tcl_path(binary)} {START:#x} bin',
                     f'verify_image {tcl_path(binary)} {START:#x} bin',
                     f'flash write_image erase {tcl_path(settings)} {SETTINGS:#x} bin',
                     f'verify_image {tcl_path(settings)} {SETTINGS:#x} bin']
        self.run(commands)

    def boot(self): self.run(['reset run'])
    def challenge(self, addr, value): self.run([f'mww {addr+56:#x} {value}'])
    def restore(self, binary):
        self.run(['reset halt',f'flash write_image erase {tcl_path(binary)} 0 bin',
                  f'verify_image {tcl_path(binary)} 0 bin'])

def settings_page(length):
    # Adafruit SDK11 bootloader_settings_t; CRC=0 means no boot-time CRC check.
    # Host performs a byte-for-byte check before rebooting.
    return struct.pack('<HHH2x5I', 1,0,0xFE,length,0,0,length,0) + b'\xff'*(4096-28)

def verify_runtime(backend, m, wait=time.sleep):
    addr = m['telemetry']
    first = struct.unpack('<16I',backend.read(addr,64))
    nonce = secrets.randbits(32)
    backend.challenge(addr,nonce); wait(1)
    last = struct.unpack('<16I',backend.read(addr,64))
    need(last[:4] == (0x534A524D,1,m['build'],3), 'ファーム診断ヘッダ／起動段階が一致しません')
    need(last[4] != first[4] and last[15] == nonce ^ 0xA537C9E1, 'ファームからの新しい応答がありません')
    return dict(zip(['magic','version','build','stage','heartbeat','battery_mv','battery_percent',
        'key_events','keys_lo','keys_hi','usb','ble','led_frames','led_power','challenge','response'],last))

def verify_usb():
    import hid
    devices = [d for d in hid.enumerate(0x1209,0x5354) if d.get('usage_page') == 0xFF60]
    need(len(devices) == 1, '対象のUSB Vialインターフェースが1台ではありません。USB接続を確認してください')
    device = hid.device()
    try:
        device.open_path(devices[0]['path'])
        device.write(bytes([0,0xFE,0]+[0]*30))
        data = bytes(device.read(32,2000))
        need(len(data) == 32 and data[4:12] == VIAL_ID, 'USBからのVial識別応答が違います')
        return {'protocol':int.from_bytes(data[:4],'little'),'id':data[4:12].hex()}
    finally: device.close()

def backup(backend, dest):
    dest.mkdir(parents=True, exist_ok=False)
    ident = backend.identify()
    flash = backend.read(0,0x100000,True)
    uicr = backend.read(0x10001000,4096)
    (dest/'flash.bin').write_bytes(flash); (dest/'uicr.bin').write_bytes(uicr)
    meta = {'device_id':ident,'flash_sha256':sha(flash),'uicr_sha256':sha(uicr)}
    save_json(dest/'backup.json',meta)
    need(sha((dest/'flash.bin').read_bytes()) == meta['flash_sha256'], 'バックアップ保存が不完全です')
    need(sha((dest/'uicr.bin').read_bytes()) == meta['uicr_sha256'], 'UICR保存が不完全です')
    return flash,uicr

def install(backend, package, dest, migrate=False, usb=verify_usb, wait=time.sleep, progress=print):
    report = {'result':'failed','steps':{},'hardware_verification_attempted':True,'backup':str(dest)}
    try:
        m,data = bundle(package); report['steps']['package']='passed'
        progress('基板を識別して全フラッシュをバックアップしています')
        flash,uicr = backup(backend,dest); report['steps']['backup']='passed'
        need(sha(flash[0xF4000:0xFE000]) == m['bootloader_sha256'],
             '既知のStokerjackブートローダーと一致しません。書き込みは行いません')
        need(struct.unpack_from('<I',uicr,0x14)[0] == 0xF4000, 'ブートローダー位置が違います')
        old_rmk = ROM_ID in flash[START:END]
        need(old_rmk or migrate, '初回ZMK→RMK移行を選んでください（ペアリング・キー設定を初期化します）')
        report['steps']['compatibility']='passed'
        settings = settings_page(len(data)); (dest/'rmk-settings.bin').write_bytes(settings)
        # Freeze verified bytes in the backup directory; never reread a mutable bundle during programming.
        binary = dest/'new-app.bin'; binary.write_bytes(data)
        progress('RMKを書き込み、フラッシュ内容を照合しています')
        backend.write_app(binary,dest/'rmk-settings.bin',migrate)
        actual = backend.read(0,0x100000)
        need(actual[START:START+len(data)] == data, 'アプリ読み戻し不一致')
        need(actual[:START] == flash[:START] and actual[0xF4000:SETTINGS] == flash[0xF4000:SETTINGS], '保護領域が変化しました')
        need(actual[SETTINGS:] == settings, 'ブート設定読み戻し不一致')
        need(backend.read(0x10001000,4096) == uicr, 'UICRが変化しました')
        if not migrate: need(actual[END:STORAGE_END] == flash[END:STORAGE_END], '保存設定が変化しました')
        report['steps']['flash_readback']='passed'
        backend.boot(); wait(4)
        progress('起動後の応答とUSB識別を確認しています')
        report['runtime'] = verify_runtime(backend,m,wait); report['steps']['runtime']='passed'
        try:
            report['usb'] = usb(); report['steps']['usb']='passed'; report['result']='passed'
        except Exception as exc:
            report['steps']['usb']='pending'; report['usb_error']=str(exc); report['result']='partial'
        return report
    except Exception as exc:
        report['error']=str(exc)
        return report
    finally:
        dest.mkdir(parents=True,exist_ok=True)
        save_json(dest/'report.json',report)

def restore_backup(backend, folder):
    folder = Path(folder)
    m=json.loads((folder/'backup.json').read_text(encoding='utf-8'))
    data=(folder/'flash.bin').read_bytes(); uicr=(folder/'uicr.bin').read_bytes()
    need(len(data)==0x100000 and sha(data)==m['flash_sha256'] and len(uicr)==4096 and sha(uicr)==m['uicr_sha256'], 'バックアップが破損しています')
    need(backend.identify()==m['device_id'], 'バックアップ元と別の基板です')
    need(backend.read(0x10001000,4096)==uicr, 'UICRが違います。自動復元を中止します')
    with tempfile.TemporaryDirectory(prefix='stokerjack-restore-') as tmp:
        p=Path(tmp)/'restore.bin'; p.write_bytes(data)
        backend.restore(p)
    need(backend.read(0,0x100000)==data, '復元後の読み戻しが一致しません')
    backend.boot()
    return {'result':'restored','device_id':m['device_id'],'runtime':'not_checked'}

def locations():
    root=Path(sys.executable).parent if getattr(sys,'frozen',False) else Path(__file__).resolve().parent
    return root, root/'openocd/bin/openocd.exe', root/'openocd/openocd/scripts'

def main():
    root,exe,scripts=locations()
    parser=argparse.ArgumentParser(description='Stokerjack OST60 RMK / ST-Link')
    parser.add_argument('action',choices=['flash','verify','restore','check-package','gui'],nargs='?',default='gui')
    parser.add_argument('--package',type=Path,default=root/'firmware')
    parser.add_argument('--openocd',default=str(exe)); parser.add_argument('--scripts',default=str(scripts))
    parser.add_argument('--serial',default=''); parser.add_argument('--migrate',action='store_true')
    parser.add_argument('--backup',type=Path)
    args=parser.parse_args()
    if args.action=='gui':
        from gui import start
        start(args); return 0
    if args.action=='check-package': bundle(args.package); print('Package SHA256 / vectors / target OK'); return 0
    logs=[]; backend=OpenOCD(args.openocd,args.scripts,args.serial,logs.append)
    dest=Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'Stokerjack/RMK'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    try:
        if args.action=='flash': report=install(backend,args.package,dest,args.migrate)
        elif args.action=='restore':
            need(args.backup is not None,'--backup が必要です'); report=restore_backup(backend,args.backup)
        else:
            m,data=bundle(args.package); backend.identify()
            need(backend.read(START,len(data))==data,'実機とファームが一致しません')
            report={'result':'passed','runtime':verify_runtime(backend,m),'usb':verify_usb()}
        dest.mkdir(parents=True,exist_ok=True); save_json(dest/'report.json',report)
        print(json.dumps(report,ensure_ascii=False,indent=2))
        return 0 if report['result'] in ('passed','restored') else 2
    except Exception as exc:
        dest.mkdir(parents=True,exist_ok=True)
        report={'result':'failed','error':str(exc)}
        save_json(dest/'report.json',report)
        print(json.dumps(report,ensure_ascii=False,indent=2))
        return 2
    finally:
        dest.mkdir(parents=True,exist_ok=True); (dest/'openocd.log').write_text('\n'.join(logs),encoding='utf-8')

if __name__=='__main__':
    for stream in (sys.stdout,sys.stderr):
        if stream is not None and hasattr(stream,'reconfigure'): stream.reconfigure(encoding='utf-8')
    try: sys.exit(main())
    except Exception as exc: print(str(exc),file=sys.stderr); sys.exit(1)
