"""Local MMD session control panel; engine calls remain in the pinned runtime."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from work_import import import_work
from camera_authoring import validate_keys, encode_vmd, atomic_bytes, load_keys
from session import Controller, atomic_json, game_processes, run_tool, stage_session, start_loader

ROOT = Path(__file__).resolve().parents[2]


class Studio:
    def __init__(self, root, test=False):
        self.root = root; self.test = test
        self.controller = None; self.loader = None; self.working = False
        self.events = queue.Queue(); self.last_error = ''; self.close_pending = False
        self.close_stop_sent=False
        self.dragging=False
        root.title('GIMMD · 创作控制台'); root.geometry('820x690'); root.minsize(760, 650)
        root.configure(background='#f4f6f9')
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TFrame', background='#f4f6f9'); style.configure('TLabel', background='#f4f6f9', font=('Microsoft YaHei UI', 10))
        style.configure('TButton', font=('Microsoft YaHei UI', 10), padding=(12, 7))
        self.build = tk.StringVar(value=str(ROOT / 'build-studio-camera-fov'))
        self.motion = tk.StringVar(); self.audio = tk.StringVar(); self.camera = tk.StringVar()
        self.camera_enabled = tk.BooleanVar(value=False)
        self.camera_manual = tk.BooleanVar(value=False)
        self.audio_offset=tk.StringVar(value="0"); self.imported_work={}
        self.camera_keys=[]; self.key_time=tk.StringVar(value="0")
        self.camera_values=[tk.StringVar(value="0") for _ in range(7)]
        self.pid = tk.StringVar(); self.position = tk.StringVar(value='0.000')
        self.state = tk.StringVar(value='选择动作和构建目录，或连接正在运行的会话。')
        self.clock = tk.StringVar(value='00:00.000 / 00:00.000'); self.loop = tk.BooleanVar()
        self.camera_result = tk.StringVar(value='启用后与动作同步播放；首帧对齐当前机位，停止时恢复位置与旋转。FOV 随轨道播放，可手动覆盖。')
        container = ttk.Frame(root, padding=22); container.pack(fill='both', expand=True)
        ttk.Label(container, text='GIMMD', font=('Segoe UI', 22, 'bold')).pack(anchor='w')
        ttk.Label(container, text='动作、时间轴与拍摄素材 · 当前角色支持男主').pack(anchor='w', pady=(0, 12))
        notebook = ttk.Notebook(container); notebook.pack(fill='both', expand=True)
        playback = ttk.Frame(notebook, padding=14); camera_tab = ttk.Frame(notebook, padding=14)
        authoring=ttk.Frame(notebook,padding=14)
        notebook.add(playback, text='动作与播放'); notebook.add(camera_tab, text='相机轨道')
        notebook.add(authoring,text='运镜编排')
        ttk.Label(authoring,text='从相机页的手动参数添加关键帧，保存运镜工程或导出可播放的相机 VMD。',wraplength=650).pack(anchor='w',pady=8)
        timing=ttk.Frame(authoring); timing.pack(anchor='w',pady=8)
        ttk.Label(timing,text='关键帧时间（秒）').pack(side='left')
        ttk.Entry(timing,textvariable=self.key_time,width=12).pack(side='left',padx=8)
        ttk.Button(timing,text='添加 / 替换此时刻',command=self.record_camera_key).pack(side='left')
        self.key_list=ttk.Treeview(authoring,columns=['time','right','up','forward','yaw','pitch','roll','fov'],show='headings',height=8,selectmode='browse')
        for column,title in zip(self.key_list['columns'],['秒','右','上','前','偏航','俯仰','滚转','FOV']):
            self.key_list.heading(column,text=title); self.key_list.column(column,width=73,anchor='center')
        self.key_list.pack(fill='both',expand=True,pady=8)
        edits=ttk.Frame(authoring); edits.pack(anchor='w',pady=6)
        ttk.Button(edits,text='取回所选参数',command=self.recall_camera_key).pack(side='left')
        ttk.Button(edits,text='删除所选关键帧',command=self.delete_camera_key).pack(side='left',padx=8)
        outputs=ttk.Frame(authoring); outputs.pack(anchor='w',pady=6)
        ttk.Button(outputs,text='保存运镜工程',command=self.save_camera_keys).pack(side='left')
        ttk.Button(outputs,text='打开运镜工程',command=self.open_camera_keys).pack(side='left',padx=8)
        ttk.Button(outputs,text='导出相机 VMD',command=self.export_camera_keys).pack(side='left')
        ttk.Label(authoring,text='先添加第 0 秒的初始机位；时间按 30 fps 对齐。FOV=0 在编排时采用 45°。回放首帧对齐当前游戏机位，导出不会自动开始播放。',wraplength=650).pack(anchor='w',pady=8)
        playback.columnconfigure(1, weight=1)
        for row, (title, variable, kind) in enumerate([('构建目录', self.build, 'build'), ('动作 VMD', self.motion, 'motion'), ('音频 WAV', self.audio, 'audio')]):
            ttk.Label(playback, text=title).grid(row=row, column=0, sticky='w', padx=(0, 10), pady=5)
            ttk.Entry(playback, textvariable=variable).grid(row=row, column=1, sticky='ew')
            ttk.Button(playback, text='选择', command=lambda v=variable, k=kind: self.browse(v, k)).grid(row=row, column=2, padx=(10, 0))
        process_row = ttk.Frame(playback); process_row.grid(row=3, column=0, columnspan=3, sticky='ew', pady=9)
        ttk.Label(process_row, text='游戏进程').pack(side='left')
        self.process_list = ttk.Combobox(process_row, textvariable=self.pid, width=15, state='readonly'); self.process_list.pack(side='left', padx=10)
        ttk.Button(process_row, text='刷新', command=self.refresh_processes).pack(side='left')
        self.start = ttk.Button(process_row, text='开始新会话', command=self.start_session); self.start.pack(side='right')
        ttk.Label(playback, textvariable=self.clock, font=('Consolas', 18)).grid(row=4, column=0, columnspan=3, sticky='w', pady=(12, 6))
        self.slider = ttk.Scale(playback, from_=0, to=1, orient='horizontal')
        self.slider.grid(row=5, column=0, columnspan=3, sticky='ew')
        self.slider.bind('<ButtonPress-1>', lambda _: setattr(self,'dragging',True))
        self.slider.bind('<ButtonRelease-1>', self.seek_slider)
        commands = ttk.Frame(playback); commands.grid(row=6, column=0, columnspan=3, sticky='ew', pady=10)
        self.buttons = {}
        for op, title in [('play', '播放 / 继续'), ('pause', '暂停'), ('stop', '停止并恢复')]:
            button = ttk.Button(commands, text=title, command=lambda o=op: self.send(o)); button.pack(side='left', padx=(0, 8)); self.buttons[op] = button
        self.loop_box = ttk.Checkbutton(commands, text='循环', variable=self.loop, command=lambda: self.send('loop-on' if self.loop.get() else 'loop-off'))
        self.loop_box.pack(side='left', padx=5)
        seek_row = ttk.Frame(playback); seek_row.grid(row=7, column=0, columnspan=3, sticky='ew')
        ttk.Entry(seek_row, textvariable=self.position, width=12).pack(side='left')
        self.seek = ttk.Button(seek_row, text='跳转（秒）', command=self.seek_text); self.seek.pack(side='left', padx=8)
        ttk.Button(seek_row, text='连接会话目录', command=self.attach).pack(side='right')
        files = ttk.Frame(playback); files.grid(row=8, column=0, columnspan=3, sticky='ew', pady=(14, 0))
        ttk.Button(files, text='保存工程', command=self.save_project).pack(side='left')
        ttk.Button(files, text='打开工程', command=self.open_project).pack(side='left', padx=8)
        ttk.Button(files,text='导入 set.ini',command=self.import_work_file).pack(side='left')
        ttk.Label(files,text='音频偏移（秒，+提前）').pack(side='left',padx=(12,4))
        ttk.Entry(files,textvariable=self.audio_offset,width=8).pack(side='left')
        ttk.Label(camera_tab, text='选择相机 VMD，在指定时刻预览换算后的坐标、旋转与视角。', wraplength=650).pack(anchor='w', pady=8)
        row = ttk.Frame(camera_tab); row.pack(fill='x', pady=5)
        ttk.Entry(row, textvariable=self.camera).pack(side='left', fill='x', expand=True)
        ttk.Button(row, text='选择', command=lambda: self.browse(self.camera, 'motion')).pack(side='left', padx=8)
        ttk.Checkbutton(camera_tab, text='在新会话中启用相机控制', variable=self.camera_enabled).pack(anchor='w', pady=8)
        ttk.Checkbutton(camera_tab,text='仅手动机位（无需相机 VMD）',variable=self.camera_manual).pack(anchor='w')
        manual=ttk.Frame(camera_tab); manual.pack(fill='x',pady=6)
        for i,title in enumerate(['向右','向上','向前','偏航 °','俯仰 °','滚转 °','FOV °']):
            r,c=divmod(i,2)
            ttk.Label(manual,text=title).grid(row=r,column=c*2,sticky='w',padx=(0,8),pady=2)
            ttk.Entry(manual,textvariable=self.camera_values[i],width=12).grid(row=r,column=c*2+1,padx=(0,18))
        controls=ttk.Frame(camera_tab); controls.pack(anchor='w')
        self.camera_apply=ttk.Button(controls,text='应用机位',command=self.apply_camera); self.camera_apply.pack(side='left')
        self.camera_reset=ttk.Button(controls,text='复位偏移',command=self.reset_camera); self.camera_reset.pack(side='left',padx=8)
        ttk.Label(camera_tab,text='位移沿初始机位的右/上/前方向，单位为游戏世界单位；FOV=0 使用轨道值或初始值。',wraplength=650).pack(anchor='w',pady=6)
        time_row=ttk.Frame(camera_tab); time_row.pack(anchor='w',pady=8)
        ttk.Label(time_row,text='时间（秒）').pack(side='left')
        ttk.Entry(time_row,textvariable=self.position,width=12).pack(side='left',padx=8)
        ttk.Button(camera_tab, text='预览指定秒数', command=self.preview_camera).pack(anchor='w')
        ttk.Label(camera_tab, textvariable=self.camera_result, wraplength=650, justify='left').pack(anchor='w', pady=8)
        ttk.Label(container, textvariable=self.state, wraplength=760).pack(anchor='w', pady=(14, 0))
        self.camera_apply.state(['disabled']); self.camera_reset.state(['disabled'])
        self.set_controls(False)
        root.protocol('WM_DELETE_WINDOW', self.close)
        if not test:
            self.refresh_processes(); root.after(200, self.tick)

    def error(self, exc):
        self.last_error = str(exc); self.state.set(self.last_error)
        if not self.test:
            messagebox.showerror('GIMMD', self.last_error, parent=self.root)

    def browse(self, variable, kind):
        if kind == 'build': value = filedialog.askdirectory(parent=self.root)
        else: value = filedialog.askopenfilename(parent=self.root, filetypes=[('素材', '*.wav' if kind == 'audio' else '*.vmd')])
        if value:
            variable.set(value)
            if variable is self.motion and not self.audio.get():
                candidates=list(Path(value).parent.glob('*.wav'))
                if len(candidates)==1: self.audio.set(str(candidates[0]))

    def refresh_processes(self):
        try:
            values = [str(pid) for pid, _ in game_processes()]; self.process_list['values'] = values
            if values and self.pid.get() not in values: self.pid.set(values[0])
            if not values: self.pid.set('')
        except Exception as exc: self.error(exc)

    def background(self, function):
        self.working = True; self.start.state(['disabled'])
        def task():
            try: self.events.put(('result', function()))
            except Exception as exc: self.events.put(('error', exc))
        threading.Thread(target=task, daemon=True).start()

    def start_session(self):
        if self.working or (self.loader and self.loader.poll() is None) or (self.controller and self.controller.status and not self.controller.status['closed']):
            self.error('请先停止当前会话'); return
        try:
            build, motion = Path(self.build.get()), Path(self.motion.get()); pid = int(self.pid.get())
            audio = Path(self.audio.get()) if self.audio.get().strip() else None
            if self.camera_enabled.get() and not self.camera_manual.get() and not self.camera.get().strip():
                raise ValueError('请选择相机 VMD')
            audio_offset=float(self.audio_offset.get())
            manual_camera=self.camera_enabled.get() and self.camera_manual.get()
            camera = Path(self.camera.get()) if self.camera_enabled.get() and not manual_camera else None
        except Exception as exc: self.error(exc); return
        self.state.set('正在准备新会话…')
        def launch():
            directory = stage_session(build, motion, audio, ROOT / 'sessions', camera=camera, manual_camera=manual_camera,audio_offset=audio_offset)
            loader = start_loader(directory, pid)
            return ('session', directory, loader)
        self.background(launch)

    def attach(self):
        if self.working or (self.loader and self.loader.poll() is None) or (self.controller and self.controller.status and not self.controller.status['closed']):
            self.error('请先停止当前会话'); return
        value = filedialog.askdirectory(parent=self.root)
        if value: self.controller = Controller(Path(value)); self.loader = None

    def set_controls(self, enabled):
        for control in [*self.buttons.values(), self.seek, self.loop_box, self.slider]:
            control.state(['!disabled'] if enabled else ['disabled'])

    def send(self, operation, seconds=0):
        try:
            if not self.controller: raise ValueError('请先开始或连接会话')
            self.controller.enqueue(operation, seconds); self.controller.pump()
        except Exception as exc: self.error(exc)

    def seek_text(self):
        try: self.send('seek', float(self.position.get()))
        except ValueError: self.error('请输入有效秒数')

    def seek_slider(self, _):
        value=self.slider.get(); self.dragging=False; self.send('seek',value)

    def poll(self):
        if not self.controller: return
        status = self.controller.poll(); self.set_controls(self.controller.fresh)
        if status:
            seconds, duration = status['seconds'], status['durationSeconds']
            self.clock.set(f'{seconds:08.3f} / {duration:08.3f} 秒')
            self.slider.configure(to=max(duration, .001))
            if not self.dragging: self.slider.set(seconds)
            self.loop.set(bool(status.get('loop')))
            states = ['准备', '播放中', '已暂停', '播放结束']
            self.state.set('会话已结束' if status['closed'] else states[status['state']] if self.controller.fresh else '会话状态已过期，控制已停用')
        camera_active=self.controller.fresh and bool(status and status.get('cameraEnabled'))
        for button in [self.camera_apply,self.camera_reset]: button.state(['!disabled'] if camera_active else ['disabled'])
        try: self.controller.pump()
        except (OSError, ValueError) as exc: self.state.set('命令尚未发送：' + str(exc))
        if self.loader and self.loader.poll() is not None:
            if self.loader.returncode: self.state.set('加载失败，请查看会话目录中的 loader.log')
            else:
                reports = list(self.controller.directory.glob('gimmd-*.probe.json'))
                if reports:
                    try:
                        report = json.loads(reports[0].read_text(encoding='utf-8-sig'))
                        result = report.get('singleTrackPlayback', {})
                        camera = report.get('cameraPlayback', {})
                        if camera.get('cleanupPending') or camera.get('aborted'):
                            self.state.set('相机：' + camera.get('status', '恢复待确认'))
                        else: self.state.set('会话结束，姿态已恢复' if result.get('localRestored') and not result.get('cleanupPending') else
                                       '会话结束：' + result.get('status', report.get('status', '未知状态')))
                    except (OSError, ValueError): pass
        unsupported=self.imported_work.get('unsupported',{})
        if unsupported and '当前不播放' not in self.state.get():
            self.state.set(self.state.get()+'；当前不播放：'+', '.join(unsupported))
        if self.close_pending:
            if self.controller.fresh and not self.close_stop_sent:
                self.send('stop'); self.close_stop_sent=True
            if (status and status['closed'] and not self.loader) or (self.loader and self.loader.poll() is not None):
                self.root.destroy()

    def tick(self):
        try:
            while True:
                kind, value = self.events.get_nowait(); self.working = False; self.start.state(['!disabled'])
                if kind == 'error': self.error(value)
                elif value[0] == 'session':
                    self.controller = Controller(value[1]); self.loader = value[2]; self.state.set('等待会话启动…')
                elif value[0] == 'camera': self.camera_result.set(value[1])
        except queue.Empty: pass
        self.poll()
        try: self.root.after(200, self.tick)
        except tk.TclError: pass

    def save_project(self):
        value = filedialog.asksaveasfilename(parent=self.root, defaultextension='.json', filetypes=[('GIMMD 工程', '*.json')])
        if value:
            try:
                offset=float(self.audio_offset.get())
                if not math.isfinite(offset) or abs(offset)>600: raise ValueError('音频偏移应为 ±600 秒内的有限数值')
                atomic_json(Path(value), {'schemaVersion': 1, 'build': self.build.get(), 'motion': self.motion.get(), 'audio': self.audio.get(), 'camera': self.camera.get(), 'cameraEnabled': self.camera_enabled.get(), 'manualCamera': self.camera_manual.get(), 'audioOffset': offset, 'importedWork': self.imported_work})
            except Exception as exc: self.error(exc)

    def open_project(self):
        value = filedialog.askopenfilename(parent=self.root, filetypes=[('GIMMD 工程', '*.json')])
        if not value: return
        self.load_project(Path(value))

    def load_project(self,path):
        try:
            project = json.loads(path.read_text(encoding='utf-8-sig'))
            if project.get('schemaVersion') != 1: raise ValueError('不支持的工程格式')
            if any(not isinstance(project.get(name,''),str) for name in ['build','motion','audio','camera']):
                raise ValueError('工程路径格式无效')
            if type(project.get('cameraEnabled', False)) is not bool or type(project.get('manualCamera', False)) is not bool:
                raise ValueError('工程相机开关无效')
            offset=project.get('audioOffset',0)
            if type(offset) not in (int,float) or not math.isfinite(offset) or abs(offset)>600:
                raise ValueError('工程音频偏移无效')
            if not isinstance(project.get('importedWork',{}),dict): raise ValueError('工程作品信息无效')
            self.audio_offset.set(str(offset)); self.imported_work=project.get('importedWork',{})
            self.camera_enabled.set(project.get('cameraEnabled', False))
            self.camera_manual.set(project.get('manualCamera', False))
            for name in ['build', 'motion', 'audio', 'camera']:
                getattr(self, name).set(project.get(name, ''))
        except Exception as exc: self.error(exc)

    def import_work_file(self):
        path=filedialog.askopenfilename(parent=self.root,filetypes=[('MMD 作品','set.ini')])
        if not path: return
        try:
            work=import_work(Path(path))
            for name in ['motion','audio','camera']: getattr(self,name).set(work[name])
            self.audio_offset.set(str(work['audioOffset'])); self.camera_enabled.set(bool(work['camera'])); self.camera_manual.set(False)
            self.imported_work=work
            unsupported=', '.join(work['unsupported'])
            self.state.set('已导入：'+work['name']+('；当前不播放这些通道：'+unsupported if unsupported else ''))
        except Exception as exc: self.error(exc)

    def refresh_camera_keys(self):
        self.key_list.delete(*self.key_list.get_children())
        for i,key in enumerate(self.camera_keys):
            self.key_list.insert('', 'end',iid=str(i),values=[f"{key['seconds']:.3f}",*[f'{v:.3f}' for v in key['values']]])

    def record_camera_key(self):
        try:
            seconds=float(self.key_time.get()); values=[float(v.get()) for v in self.camera_values]
            if values[6]==0: values[6]=45
            import math
            if not math.isfinite(seconds) or not 0<=seconds<=86400: raise ValueError('关键帧时间超出范围')
            frame=int(math.floor(seconds*30+.5))
            candidate=[k for k in self.camera_keys if round(k['seconds']*30)!=frame]
            candidate.append({'seconds':frame/30,'values':values})
            self.camera_keys=validate_keys(candidate); self.refresh_camera_keys()
        except Exception as exc: self.error(exc)

    def recall_camera_key(self):
        selected=self.key_list.selection()
        if selected:
            key=self.camera_keys[int(selected[0])]; self.key_time.set(str(key['seconds']))
            for value,number in zip(self.camera_values,key['values']): value.set(str(number))

    def delete_camera_key(self):
        selected=self.key_list.selection()
        if selected: self.camera_keys.pop(int(selected[0])); self.refresh_camera_keys()

    def save_camera_keys(self):
        try: keys=validate_keys(self.camera_keys)
        except Exception as exc: self.error(exc); return
        path=filedialog.asksaveasfilename(parent=self.root,defaultextension='.json',filetypes=[('运镜工程','*.json')])
        if path:
            try: atomic_json(Path(path),{'schemaVersion':1,'type':'gimmd-camera-keys','keys':keys})
            except Exception as exc: self.error(exc)

    def open_camera_keys(self):
        path=filedialog.askopenfilename(parent=self.root,filetypes=[('运镜工程','*.json')])
        if path:
            try: self.camera_keys=load_keys(Path(path)); self.refresh_camera_keys()
            except Exception as exc: self.error(exc)

    def export_camera_keys(self):
        try: data=encode_vmd(self.camera_keys)
        except Exception as exc: self.error(exc); return
        path=filedialog.asksaveasfilename(parent=self.root,defaultextension='.vmd',filetypes=[('相机 VMD','*.vmd')])
        if path:
            try:
                atomic_bytes(Path(path),data); self.camera.set(path); self.camera_enabled.set(True); self.camera_manual.set(False)
                self.state.set('相机 VMD 已导出并选入工程；开始新会话时播放。')
            except Exception as exc: self.error(exc)

    def apply_camera(self):
        try:
            if not self.controller: raise ValueError('请先启用相机并开始会话')
            sequence=self.controller.camera([float(v.get()) for v in self.camera_values])
            self.camera_result.set(f'相机命令 {sequence} 已发送，等待运行时应用。')
        except Exception as exc: self.error(exc)

    def reset_camera(self):
        for value in self.camera_values: value.set('0')
        self.apply_camera()

    def preview_camera(self):
        if self.working: return
        try: seconds = float(self.position.get())
        except ValueError: self.error('请输入有效秒数'); return
        executable, path = Path(self.build.get()) / 'gimmd-camera.exe', Path(self.camera.get())
        def inspect():
            sample = run_tool([executable, path, str(seconds)])
            return ('camera', f"时间 {sample['seconds']:.3f} 秒\n位置 {sample['position']}\n旋转四元数 {sample['rotation']}\n视角 {sample['fov']:.2f}°\n投影：{'正交' if sample['orthographic'] else '透视'}\n仅离线预览，未控制游戏相机。")
        self.background(inspect)

    def close(self):
        if self.working:
            self.error('正在准备会话，请等待准备结束'); return
        if (self.controller and self.controller.fresh) or (self.loader and self.loader.poll() is None):
            if self.controller and self.controller.fresh:
                self.send('stop'); self.close_stop_sent=True
            self.close_pending = True; self.state.set('正在停止并等待恢复…')
        else: self.root.destroy()


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--session', type=Path); parser.add_argument('--project',type=Path); args = parser.parse_args()
    root = tk.Tk(); app = Studio(root)
    if args.session: app.controller = Controller(args.session)
    if args.project: app.load_project(args.project)
    root.mainloop()


if __name__ == '__main__': main()
