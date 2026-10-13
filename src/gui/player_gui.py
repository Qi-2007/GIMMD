"""Local MMD session control panel; engine calls remain in the pinned runtime."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
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
        self.build = tk.StringVar(value=str(ROOT / 'build-camera-player'))
        self.motion = tk.StringVar(); self.audio = tk.StringVar(); self.camera = tk.StringVar()
        self.camera_enabled = tk.BooleanVar(value=False)
        self.pid = tk.StringVar(); self.position = tk.StringVar(value='0.000')
        self.state = tk.StringVar(value='选择动作和构建目录，或连接正在运行的会话。')
        self.clock = tk.StringVar(value='00:00.000 / 00:00.000'); self.loop = tk.BooleanVar()
        self.camera_result = tk.StringVar(value='启用后与动作同步播放；首帧对齐当前机位，停止时恢复位置与旋转。FOV 暂不接管。')
        container = ttk.Frame(root, padding=22); container.pack(fill='both', expand=True)
        ttk.Label(container, text='GIMMD', font=('Segoe UI', 22, 'bold')).pack(anchor='w')
        ttk.Label(container, text='动作、时间轴与拍摄素材 · 当前角色支持男主').pack(anchor='w', pady=(0, 12))
        notebook = ttk.Notebook(container); notebook.pack(fill='both', expand=True)
        playback = ttk.Frame(notebook, padding=14); camera_tab = ttk.Frame(notebook, padding=14)
        notebook.add(playback, text='动作与播放'); notebook.add(camera_tab, text='相机轨道')
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
        ttk.Label(camera_tab, text='选择相机 VMD，在指定时刻预览换算后的坐标、旋转与视角。', wraplength=650).pack(anchor='w', pady=8)
        row = ttk.Frame(camera_tab); row.pack(fill='x', pady=10)
        ttk.Entry(row, textvariable=self.camera).pack(side='left', fill='x', expand=True)
        ttk.Button(row, text='选择', command=lambda: self.browse(self.camera, 'motion')).pack(side='left', padx=8)
        ttk.Checkbutton(camera_tab, text='在新会话中启用此相机轨道（位置与旋转）', variable=self.camera_enabled).pack(anchor='w', pady=8)
        time_row=ttk.Frame(camera_tab); time_row.pack(anchor='w',pady=8)
        ttk.Label(time_row,text='时间（秒）').pack(side='left')
        ttk.Entry(time_row,textvariable=self.position,width=12).pack(side='left',padx=8)
        ttk.Button(camera_tab, text='预览指定秒数', command=self.preview_camera).pack(anchor='w')
        ttk.Label(camera_tab, textvariable=self.camera_result, wraplength=650, justify='left').pack(anchor='w', pady=20)
        ttk.Label(container, textvariable=self.state, wraplength=760).pack(anchor='w', pady=(14, 0))
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
            if self.camera_enabled.get() and not self.camera.get().strip():
                raise ValueError('请选择相机 VMD')
            camera = Path(self.camera.get()) if self.camera_enabled.get() else None
        except Exception as exc: self.error(exc); return
        self.state.set('正在准备新会话…')
        def launch():
            directory = stage_session(build, motion, audio, ROOT / 'sessions', camera=camera)
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
            try: atomic_json(Path(value), {'schemaVersion': 1, 'build': self.build.get(), 'motion': self.motion.get(), 'audio': self.audio.get(), 'camera': self.camera.get(), 'cameraEnabled': self.camera_enabled.get()})
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
            if type(project.get('cameraEnabled', False)) is not bool:
                raise ValueError('工程相机开关无效')
            self.camera_enabled.set(project.get('cameraEnabled', False))
            for name in ['build', 'motion', 'audio', 'camera']:
                getattr(self, name).set(project.get(name, ''))
        except Exception as exc: self.error(exc)

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
