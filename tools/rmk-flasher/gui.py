"""Small Windows GUI; all probe work runs off the Tk event thread."""
import json, os, queue, threading, time
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from stokerjack_flash import OpenOCD, bundle, install, restore_backup, verify_runtime, verify_usb, START, need, save_json

def start(args):
    app=tk.Tk(); app.title('Stokerjack RMK — OST砲弾型 / 60キー'); app.geometry('820x630')
    frame=ttk.Frame(app,padding=18); frame.pack(fill='both',expand=True)
    ttk.Label(frame,text='Stokerjack RMK',font=('Yu Gothic UI',22,'bold')).pack(anchor='w')
    ttk.Label(frame,text='ST-Linkと基板USBを接続してください。初回は全フラッシュを保存してから移行します。').pack(anchor='w',pady=5)
    ttk.Label(frame,text='実機未検証の候補版です。4灯の発光・全キー・Bluetooth・電池動作は別途実機確認が必要です。',wraplength=760).pack(anchor='w')
    form=ttk.Frame(frame); form.pack(fill='x',pady=12)
    package=tk.StringVar(value=str(args.package)); serial=tk.StringVar(value=args.serial); migrate=tk.BooleanVar(value=args.migrate)
    ttk.Label(form,text='ファームフォルダー').grid(row=0,column=0,sticky='w')
    ttk.Entry(form,textvariable=package,width=65).grid(row=0,column=1,sticky='ew')
    def pick():
        p=filedialog.askdirectory();
        if p: package.set(p)
    ttk.Button(form,text='選択',command=pick).grid(row=0,column=2)
    ttk.Label(form,text='ST-Linkシリアル').grid(row=1,column=0,sticky='w')
    ttk.Entry(form,textvariable=serial).grid(row=1,column=1,sticky='ew')
    ttk.Label(form,text='空欄ならST-Linkを1台だけ接続').grid(row=2,column=1,sticky='w')
    form.columnconfigure(1,weight=1)
    ttk.Checkbutton(frame,text='初回ZMK→RMK移行／RMK設定初期化（ペアリング・キー設定を消去）',variable=migrate).pack(anchor='w')
    status=tk.StringVar(value='待機中 — 実機の成功確認はまだ行っていません')
    ttk.Label(frame,textvariable=status,wraplength=760,font=('Yu Gothic UI',11,'bold')).pack(anchor='w',pady=14)
    buttons=ttk.Frame(frame); buttons.pack(fill='x')
    log=tk.Text(frame,height=17,wrap='word'); log.pack(fill='both',expand=True,pady=10)
    messages=queue.Queue(); controls=[]; active=False
    def begin(action):
        nonlocal active
        if active: return
        backup_path=None
        if action=='restore':
            backup_path=filedialog.askdirectory(title='backup.json / flash.bin / uicr.bin のフォルダー')
            if not backup_path: return
        if action=='flash' and migrate.get():
            if not messagebox.askyesno('初回移行／初期化','バックアップ後、RMKへ移行してペアリング・キー設定を初期化します。続けますか？'): return
        active=True
        for b in controls: b.configure(state='disabled')
        package_path=Path(package.get()); serial_value=serial.get(); migrate_value=migrate.get()
        status.set('処理中。電源・SWDを抜かないでください')
        def worker():
            logs=[]
            dest=Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'Stokerjack/RMK'/datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            def output(s): logs.append(s); messages.put(('log',s))
            backend=OpenOCD(args.openocd,args.scripts,serial_value,output)
            try:
                if action=='flash': report=install(backend,package_path,dest,migrate_value,progress=lambda s: messages.put(('status',s)))
                elif action=='restore': report=restore_backup(backend,backup_path)
                else:
                    m,data=bundle(package_path); ident=backend.identify()
                    need(backend.read(START,len(data))==data,'実機のファームがこのパッケージと違います')
                    report={'result':'partial','device_id':ident,'runtime':verify_runtime(backend,m)}
                    try: report['usb']=verify_usb(); report['result']='passed'
                    except Exception as exc: report['usb_error']=str(exc)
                messages.put(('log',json.dumps(report,ensure_ascii=False,indent=2)))
                labels={'passed':'書き込み内容・実行応答・USB識別を確認しました（キー・LEDの実測は別途）',
                        'partial':'一部確認できました。USBなど未確認の項目をログで確認してください',
                        'restored':'元のフラッシュを復元・照合しました。元ファームの動作は実機で確認してください',
                        'failed':'完了していません。ログを確認してください'}
                messages.put(('status',labels[report['result']]))
            except Exception as exc:
                report={'result':'failed','error':str(exc)}; messages.put(('status','失敗: '+str(exc)))
            finally:
                dest.mkdir(parents=True,exist_ok=True); save_json(dest/'report.json',report)
                (dest/'openocd.log').write_text('\n'.join(logs),encoding='utf-8')
                messages.put(('log','記録: '+str(dest))); messages.put(('done',''))
        threading.Thread(target=worker,daemon=True).start()
    for label,action in [('バックアップ＋自動書き込み','flash'),('書き込み・起動を再確認','verify'),('元のファームへ復元','restore')]:
        b=ttk.Button(buttons,text=label,command=lambda a=action: begin(a)); b.pack(side='left',padx=(0,8)); controls.append(b)
    def poll():
        nonlocal active
        while not messages.empty():
            kind,value=messages.get()
            if kind=='log': log.insert('end',value+'\n'); log.see('end')
            elif kind=='status': status.set(value)
            elif kind=='done':
                active=False
                for b in controls: b.configure(state='normal')
        app.after(100,poll)
    def close():
        if active: messagebox.showinfo('処理中','書き込み／照合が終わってから閉じてください。'); return
        app.destroy()
    app.protocol('WM_DELETE_WINDOW',close); poll(); app.mainloop()
