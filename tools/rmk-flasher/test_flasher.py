import json, struct, tempfile, unittest
from pathlib import Path
import stokerjack_flash as f

class Fake:
    def __init__(self):
        self.flash=bytearray(b'\xff'*0x100000); self.uicr=bytearray(b'\xff'*4096)
        struct.pack_into('<I',self.uicr,0x14,0xF4000)
        self.writes=0; self.nonce=0; self.beat=0; self.bad=False; self.stuck=False; self.id='device-A'
    def identify(self): return self.id
    def read(self,a,n,halt=False):
        if a==0x10001000: return bytes(self.uicr)
        if a==0x20001000:
            if not self.stuck: self.beat+=1
            return struct.pack('<16I',0x534A524D,1,0x20261017,3,self.beat,2500,45,0,0,0,1,0,0,0,self.nonce,self.nonce^0xA537C9E1)
        return bytes(self.flash[a:a+n])
    def write_app(self,b,s,migrate):
        self.writes+=1; data=b.read_bytes(); self.flash[f.START:f.START+len(data)]=data
        self.flash[f.SETTINGS:]=s.read_bytes()
        if migrate: self.flash[f.END:f.STORAGE_END]=b'\xff'*(f.STORAGE_END-f.END)
        if self.bad: self.flash[0x1010]^=1
    def boot(self): pass
    def challenge(self,a,n): self.nonce=n
    def restore(self,p): self.writes+=1; self.flash[:]=p.read_bytes()

class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.root=Path(self.tmp.name); self.pkg=self.root/'pkg'; self.pkg.mkdir()
        self.hw=Fake(); data=struct.pack('<II',0x20040000,0x1101)+b'\0'*256+f.ROM_ID
        (self.pkg/'stokerjack-rmk.bin').write_bytes(data)
        self.m={'target':'Stokerjack-OST60-nRF52840','start':f.START,'bin_sha256':f.sha(data),
                'telemetry':0x20001000,'build':0x20261017,'bootloader_sha256':f.sha(self.hw.flash[0xF4000:0xFE000])}
        f.save_json(self.pkg/'manifest.json',self.m)
    def tearDown(self): self.tmp.cleanup()
    def install(self,**kw):
        return f.install(self.hw,self.pkg,self.root/'backup',wait=lambda _:None,usb=lambda:{'id':'valid'},progress=lambda _:None,**kw)
    def test_success_and_full_restore(self):
        before=bytes(self.hw.flash); r=self.install(migrate=True); self.assertEqual(r['result'],'passed')
        f.restore_backup(self.hw,self.root/'backup'); self.assertEqual(bytes(self.hw.flash),before)
    def test_no_migration_does_not_write(self):
        self.assertEqual(self.install()['result'],'failed'); self.assertEqual(self.hw.writes,0)
    def test_corrupt_package_no_write(self):
        (self.pkg/'stokerjack-rmk.bin').write_bytes(b'bad'); self.assertEqual(self.install(migrate=True)['result'],'failed'); self.assertEqual(self.hw.writes,0)
    def test_unknown_bootloader_no_write(self):
        self.hw.flash[0xF4000]=0; self.assertEqual(self.install(migrate=True)['result'],'failed'); self.assertEqual(self.hw.writes,0)
    def test_readback_failure_is_not_success(self):
        self.hw.bad=True; self.assertEqual(self.install(migrate=True)['result'],'failed')
    def test_stuck_firmware_is_not_success(self):
        self.hw.stuck=True; self.assertEqual(self.install(migrate=True)['result'],'failed')
    def test_usb_missing_is_partial(self):
        def missing(): raise RuntimeError('USB missing')
        r=f.install(self.hw,self.pkg,self.root/'backup',migrate=True,wait=lambda _:None,usb=missing,progress=lambda _:None)
        self.assertEqual(r['result'],'partial'); self.assertEqual(r['steps']['runtime'],'passed')
    def test_wrong_device_restore_rejected(self):
        self.install(migrate=True); self.hw.id='device-B'; count=self.hw.writes
        with self.assertRaisesRegex(RuntimeError,'別の基板'): f.restore_backup(self.hw,self.root/'backup')
        self.assertEqual(self.hw.writes,count)
    def test_corrupt_backup_restore_rejected(self):
        self.install(migrate=True); (self.root/'backup/flash.bin').write_bytes(b'bad'); count=self.hw.writes
        with self.assertRaises(RuntimeError): f.restore_backup(self.hw,self.root/'backup')
        self.assertEqual(self.hw.writes,count)
    def test_update_preserves_storage(self):
        self.hw.flash[0x2000:0x2000+len(f.ROM_ID)]=f.ROM_ID
        self.hw.flash[f.END:f.END+4]=b'test'
        self.assertEqual(self.install()['result'],'passed'); self.assertEqual(self.hw.flash[f.END:f.END+4],b'test')
    def test_settings_struct(self):
        p=f.settings_page(1234); self.assertEqual(len(p),4096)
        self.assertEqual(struct.unpack_from('<HHH',p),(1,0,0xFE)); self.assertEqual(struct.unpack_from('<I',p,8)[0],1234)
    def test_tcl_injection_path_rejected(self):
        with self.assertRaises(RuntimeError): f.tcl_path('bad}path')

if __name__=='__main__': unittest.main()
