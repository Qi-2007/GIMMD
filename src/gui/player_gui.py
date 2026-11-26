"""Local MMD session control panel; engine calls remain in the pinned runtime."""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from work_import import import_work
from camera_authoring import validate_keys, encode_vmd, atomic_bytes, load_keys, sample_keys
from session import Controller, atomic_json, game_processes, run_tool, stage_session, start_loader, validate_uid_text, validate_camera_adjustments
from session import PHYSICS_DEFAULTS, PHYSICS_RANGES, validate_physics_controls

ROOT = Path(__file__).resolve().parents[2]
def actor_choices():
    labels_path=ROOT/'data'/'actor-labels.json'
    labels=json.loads(labels_path.read_text(encoding='utf-8-sig')) if labels_path.exists() else {}
    catalog_path=ROOT/'src'/'generated'/'gi_actor_resource_ids.json'
    if not catalog_path.exists():
        return {'旅行者（空）':'PlayerBoy','莉奈娅':'Linnea','哥伦比娅':'Columbina','沃雅妮莎':'Vodyanitsa','奥黛塔':'Odette'}
    roles=json.loads(catalog_path.read_text(encoding='utf-8-sig'))['roles']
    return {labels.get(role['selector'],role['selector']):role['selector'] for role in roles if role['selector']!='Nahida_Show01'}
ACTOR_CHOICES=actor_choices()


class Studio:
    def __init__(self, root, test=False):
        self.root = root; self.test = test
        self.controller = None; self.loader = None; self.working = False
        self.events = queue.Queue(); self.last_error = ''; self.close_pending = False
        self.close_stop_sent=False
        self.dragging=False
        self.author_preview=None; self.preview_generation=0
        root.title('GIMMD · 创作控制台 · 7.2 动作与角色物理'); root.geometry('820x690'); root.minsize(760, 650)
        root.configure(background='#f4f6f9')
        style = ttk.Style(root); style.theme_use('clam')
        style.configure('TFrame', background='#f4f6f9'); style.configure('TLabel', background='#f4f6f9', font=('Microsoft YaHei UI', 10))
        style.configure('TButton', font=('Microsoft YaHei UI', 10), padding=(12, 7))
        self.build = tk.StringVar(value=str(ROOT / 'build-studio-7.2-face'))
        self.actor_motion_slot=tk.StringVar(value='1')
        self.actor_source=tk.StringVar(value='当前角色')
        self.actor_result=tk.StringVar(value='连接新版会话后可生成本地可见模型。')
        self.motion = tk.StringVar(); self.audio = tk.StringVar(); self.camera = tk.StringVar()
        self.camera_enabled = tk.BooleanVar(value=False)
        self.camera_manual = tk.BooleanVar(value=False)
        self.braid_static=tk.BooleanVar(value=False); self.pause_bgm=tk.BooleanVar(value=False)
        self.uid_enabled=tk.BooleanVar(value=False); self.uid_text=tk.StringVar(value='MMD 创作中')
        self.cloth_static=tk.BooleanVar(value=False)
        self.audio_offset=tk.StringVar(value="0"); self.imported_work={}
        self.camera_keys=[]; self.key_time=tk.StringVar(value="0")
        self.hold_at_end=tk.BooleanVar(value=True)
        self.fov_multiplier=tk.StringVar(value="1")
        self.camera_values=[tk.StringVar(value="0") for _ in range(7)]
        self.face_enabled=tk.BooleanVar(value=False)
        self.physics_enabled=tk.BooleanVar(value=False)
        self.physics_values={key:tk.StringVar(value=str(value)) for key,value in PHYSICS_DEFAULTS.items()}
        self.front_camera=tk.BooleanVar(value=True)
        self.pid = tk.StringVar(); self.position = tk.StringVar(value='0.000')
        self.state = tk.StringVar(value='选择动作和构建目录，或连接正在运行的会话。')
        self.clock = tk.StringVar(value='00:00.000 / 00:00.000'); self.loop = tk.BooleanVar()
        self.camera_result = tk.StringVar(value='启用后与动作同步播放；首帧对齐当前机位，停止时恢复位置与旋转。FOV 随轨道播放，可手动覆盖。')
        container = ttk.Frame(root, padding=22); container.pack(fill='both', expand=True)
        ttk.Label(container, text='GIMMD', font=('Segoe UI', 22, 'bold')).pack(anchor='w')
        ttk.Label(container, text='当前角色：先还原默认骨骼姿态，再播放 · 演员：直接加载或复制后播放').pack(anchor='w', pady=(0, 12))
        notebook = ttk.Notebook(container); notebook.pack(fill='both', expand=True)
        playback = ttk.Frame(notebook, padding=14); camera_tab = ttk.Frame(notebook, padding=8)
        authoring=ttk.Frame(notebook,padding=10)
        notebook.add(playback, text='动作与播放'); notebook.add(camera_tab, text='相机轨道')
        notebook.add(authoring,text='运镜编排')
        shooting=ttk.Frame(notebook,padding=14); notebook.add(shooting,text='拍摄辅助')
        physics_tab=ttk.Frame(notebook,padding=14); notebook.add(physics_tab,text='角色物理')
        self.physics_tab=physics_tab; self.notebook=notebook
        actors=ttk.Frame(notebook,padding=14); notebook.add(actors,text='本地演员')
        ttk.Label(actors,text='最多创建三名演员。选择一名播放全身 FK 与根位移，支持暂停、结束定格和恢复；暂不含脚部 IK、音频及相机同步。',wraplength=650).pack(anchor='w',pady=10)
        ttk.Label(actors,text='角色（直接加载无需先打开预览）').pack(anchor='w',pady=6)
        ttk.Combobox(actors,textvariable=self.actor_source,values=list(ACTOR_CHOICES)+['当前角色','Ambor'],width=38).pack(anchor='w')
        actor_buttons=ttk.Frame(actors); actor_buttons.pack(anchor='w',pady=12)
        ttk.Button(actor_buttons,text='直接加载并创建',command=lambda:self.actor_command('spawn-template')).pack(side='left')
        ttk.Button(actor_buttons,text='复制已载入模型',command=lambda:self.actor_command('spawn')).pack(side='left',padx=8)
        ttk.Button(actor_buttons,text='移除全部演员',command=lambda:self.actor_command('clear')).pack(side='left',padx=8)
        ttk.Button(actor_buttons,text='检查演员',command=lambda:self.actor_command('check')).pack(side='left',padx=8)
        motion_buttons=ttk.Frame(actors);motion_buttons.pack(anchor='w',pady=6)
        ttk.Label(motion_buttons,text='演员槽位').pack(side='left')
        ttk.Combobox(motion_buttons,textvariable=self.actor_motion_slot,values=['1','2','3'],state='readonly',width=3).pack(side='left',padx=5)
        for label,operation in [('播放所选 VMD','motion-play'),('暂停','motion-pause'),('继续','motion-resume'),('恢复姿态','motion-restore')]:
            ttk.Button(motion_buttons,text=label,command=lambda op=operation:self.actor_motion_command(op)).pack(side='left',padx=3)
        shooting_buttons=ttk.Frame(actors);shooting_buttons.pack(anchor='w',pady=6)
        for label,op in [('隐藏当前角色','hide-player'),('显示当前角色','show-player'),('移到所选槽位','camera-move'),('跟随所选槽位','camera-follow'),('恢复相机','camera-release')]:
            ttk.Button(shooting_buttons,text=label,command=lambda action=op:self.actor_shooting_command(action)).pack(side='left',padx=2)
        ttk.Label(actors,text='相机使用上方演员槽位。请先在相机页启用手动相机；定位和跟随保持镜头角度，可继续调整相机参数。',wraplength=650).pack(anchor='w')
        ttk.Label(actors,textvariable=self.actor_result,wraplength=650).pack(anchor='w',pady=10)
        ttk.Label(actors,text='直接加载支持目录中的角色，首次加载可能短暂卡顿。当前角色和 Ambor 请用“复制已载入模型”。预览模型的复制会独立保留材质资源；清空演员后资源保留至会话结束。本会话最多执行 16 次资源加载，达到上限后请结束并重新开启会话。',wraplength=650).pack(anchor='w',pady=10)
        ttk.Label(shooting,text='播放结束自动暂停；恢复游戏动画不会关闭相机或 UID。',wraplength=650).pack(anchor='w',pady=10)
        ttk.Label(shooting,text='当前角色播放前会暂停辅助驱动，并还原有参考数据的骨骼位置与旋转；停止后恢复姿态和驱动。其他角色支持 FK、根位移和腰部辅助骨；腿/脚尖 IK 与额外固定骨骼功能仍仅使用男主标定。',wraplength=650).pack(anchor='w',pady=6)
        ttk.Checkbutton(shooting,text='固定辫子（男主后脑辫子骨骼）',variable=self.braid_static).pack(anchor='w',pady=10)
        ttk.Checkbutton(shooting,text='尝试暂停游戏背景音乐',variable=self.pause_bgm).pack(anchor='w',pady=10)
        ttk.Label(shooting,text='实验功能：仅暂停已识别的音乐分类；未找到接口时不生效，动作音频继续播放。',wraplength=650).pack(anchor='w')
        ttk.Checkbutton(shooting,text='替换右下角 UID 区域显示文字',variable=self.uid_enabled).pack(anchor='w',pady=(20,8))
        ttk.Entry(shooting,textvariable=self.uid_text,width=60).pack(anchor='w',fill='x')
        ttk.Button(shooting,text='应用 UID 文字（当前会话）',command=self.apply_uid).pack(anchor='w',pady=6)
        ttk.Label(shooting,text='最多 64 字；留空可清空文字。仅修改画面文字，不修改账号 UID。UID 可应用至当前会话；停止时恢复。',wraplength=650).pack(anchor='w',pady=10)

        ttk.Checkbutton(physics_tab,text='启用当前角色动态物理（实验）',variable=self.physics_enabled).pack(anchor='w',pady=(4,10))
        ttk.Label(physics_tab,text='头发、衣物随动作摆动。需关闭披风/辫子静态稳定，暂不适用于载入演员。',wraplength=650).pack(anchor='w')
        settings_box=ttk.LabelFrame(physics_tab,text='裙摆上段 · 前后独立避让',padding=8); settings_box.pack(fill='x',pady=8)
        for column,title in enumerate(['位置','最大角度（°）','接触余量（厘米）','过渡时间（毫秒）']):
            ttk.Label(settings_box,text=title).grid(row=0,column=column,padx=8,pady=4,sticky='w')
        for row,(prefix,label) in enumerate([('front','前侧'),('rear','后侧')],1):
            ttk.Label(settings_box,text=label).grid(row=row,column=0,padx=8,pady=4,sticky='w')
            for column,(suffix,increment) in enumerate([('Angle',1),('PaddingCm',.1),('ResponseMs',10)],1):
                key=prefix+suffix; low,high=PHYSICS_RANGES[key]
                ttk.Spinbox(settings_box,textvariable=self.physics_values[key],from_=low,to=high,increment=increment,width=13).grid(row=row,column=column,padx=8,pady=4,sticky='w')
        ttk.Label(physics_tab,text='角度越大，可抬起越多；余量越大，越早避开腿部；过渡时间越长，变化越缓和。侧面裙片自动在前后参数之间过渡。',wraplength=650).pack(anchor='w',pady=6)
        support_row=ttk.Frame(physics_tab);support_row.pack(anchor='w',pady=8)
        ttk.Label(support_row,text='裙形支撑（原始开口 %）').pack(side='left',padx=(0,12))
        ttk.Spinbox(support_row,textvariable=self.physics_values['skirtSupportPercent'],from_=0,to=100,increment=1,width=10).pack(side='left')
        ttk.Label(physics_tab,text='支撑越高越不易贴腿；0 表示关闭这项支撑，仍保留碰撞与摆动。',wraplength=650).pack(anchor='w')
        ttk.Button(physics_tab,text='恢复已验证的默认参数',command=self.reset_physics_controls).pack(anchor='w',pady=6)
        ttk.Label(physics_tab,text='参数随工程保存。修改后点击“恢复游戏动画”，再点“播放”生效，无需重新连接；暂停后继续不会重新载入参数。',wraplength=650).pack(anchor='w',pady=4)
        ttk.Label(authoring,text='从相机页的手动参数添加关键帧，保存运镜工程或导出可播放的相机 VMD。',wraplength=650).pack(anchor='w',pady=8)
        timing=ttk.Frame(authoring); timing.pack(anchor='w',pady=8)
        ttk.Label(timing,text='关键帧时间（秒）').pack(side='left')
        ttk.Entry(timing,textvariable=self.key_time,width=12).pack(side='left',padx=8)
        ttk.Button(timing,text='添加 / 替换此时刻',command=self.record_camera_key).pack(side='left')
        self.key_list=ttk.Treeview(authoring,columns=['time','right','up','forward','yaw','pitch','roll','fov'],show='headings',height=6,selectmode='browse')
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
        previews=ttk.Frame(authoring); previews.pack(anchor='w',pady=6)
        ttk.Button(previews,text='游戏内预览此时刻',command=self.preview_author_frame).pack(side='left')
        ttk.Button(previews,text='游戏内播放运镜',command=self.play_author_preview).pack(side='left',padx=8)
        ttk.Button(previews,text='返回会话机位',command=self.stop_author_preview).pack(side='left')
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
        self.start = ttk.Button(process_row, text='连接并注入（待命）', command=self.start_session); self.start.pack(side='right')
        ttk.Label(playback, textvariable=self.clock, font=('Consolas', 18)).grid(row=4, column=0, columnspan=3, sticky='w', pady=(12, 6))
        self.slider = ttk.Scale(playback, from_=0, to=1, orient='horizontal')
        self.slider.grid(row=5, column=0, columnspan=3, sticky='ew')
        self.slider.bind('<ButtonPress-1>', lambda _: setattr(self,'dragging',True))
        self.slider.bind('<ButtonRelease-1>', self.seek_slider)
        commands = ttk.Frame(playback); commands.grid(row=6, column=0, columnspan=3, sticky='ew', pady=10)
        self.buttons = {}
        for op, title in [('play', '播放 / 继续'), ('pause', '暂停'), ('stop', '恢复游戏动画')]:
            button = ttk.Button(commands, text=title, command=lambda o=op: self.send(o)); button.pack(side='left', padx=(0, 8)); self.buttons[op] = button
        self.loop_box = ttk.Checkbutton(commands, text='循环', variable=self.loop, command=lambda: self.send('loop-on' if self.loop.get() else 'loop-off'))
        self.loop_box.pack(side='left', padx=5)
        ttk.Checkbutton(commands,text='披风静态稳定（新会话）',variable=self.cloth_static).pack(side='right')
        seek_row = ttk.Frame(playback); seek_row.grid(row=7, column=0, columnspan=3, sticky='ew')
        ttk.Entry(seek_row, textvariable=self.position, width=12).pack(side='left')
        self.seek = ttk.Button(seek_row, text='跳转（秒）', command=self.seek_text); self.seek.pack(side='left', padx=8)
        ttk.Button(seek_row, text='连接会话目录', command=self.attach).pack(side='right')
        ttk.Button(seek_row,text='退出注入会话',command=lambda:self.send('exit')).pack(side='right',padx=6)
        files = ttk.Frame(playback); files.grid(row=8, column=0, columnspan=3, sticky='ew', pady=(14, 0))
        ttk.Button(files, text='保存工程', command=self.save_project).pack(side='left')
        ttk.Button(files, text='打开工程', command=self.open_project).pack(side='left', padx=8)
        ttk.Button(files,text='导入 set.ini',command=self.import_work_file).pack(side='left')
        ttk.Checkbutton(playback,text='播放前相机归位（正对角色，清零手动偏移）',variable=self.front_camera).grid(row=9,column=0,columnspan=3,sticky='w',pady=8)
        ttk.Checkbutton(playback,text='播放 VMD 内的面部表情（实验：眨眼、笑眼、张嘴、部分眉形）',variable=self.face_enabled).grid(row=10,column=0,columnspan=3,sticky='w',pady=4)
        ttk.Label(files,text='音频偏移（秒，+提前）').pack(side='left',padx=(12,4))
        ttk.Entry(files,textvariable=self.audio_offset,width=8).pack(side='left')
        ttk.Label(camera_tab, text='选择相机 VMD，在指定时刻预览换算后的坐标、旋转与视角。', wraplength=650).pack(anchor='w', pady=8)
        row = ttk.Frame(camera_tab); row.pack(fill='x', pady=5)
        ttk.Entry(row, textvariable=self.camera).pack(side='left', fill='x', expand=True)
        ttk.Button(row, text='选择', command=lambda: self.browse(self.camera, 'motion')).pack(side='left', padx=8)
        ttk.Label(camera_tab,text='连接后点击开启相机；关闭控制会恢复原机位，可随后切换相机素材。',wraplength=650).pack(anchor='w',pady=8)
        ttk.Checkbutton(camera_tab,text='仅手动机位（无需相机 VMD）',variable=self.camera_manual).pack(anchor='w')
        manual=ttk.Frame(camera_tab); manual.pack(fill='x',pady=6)
        for i,title in enumerate(['向右','向上','向前','偏航 °','俯仰 °','滚转 °','FOV °']):
            r,c=divmod(i,2)
            ttk.Label(manual,text=title).grid(row=r,column=c*2,sticky='w',padx=(0,8),pady=2)
            ttk.Entry(manual,textvariable=self.camera_values[i],width=12).grid(row=r,column=c*2+1,padx=(0,18))
        ttk.Label(manual,text='FOV 倍率').grid(row=3,column=2,sticky='w',padx=(0,8),pady=2)
        ttk.Entry(manual,textvariable=self.fov_multiplier,width=12).grid(row=3,column=3,padx=(0,18))
        controls=ttk.Frame(camera_tab); controls.pack(anchor='w')
        self.camera_apply=ttk.Button(controls,text='应用机位',command=self.apply_camera); self.camera_apply.pack(side='left')
        self.camera_reset=ttk.Button(controls,text='复位偏移',command=self.reset_camera); self.camera_reset.pack(side='left',padx=8)
        ttk.Button(controls,text='开启相机',command=lambda:self.send('camera-on')).pack(side='left')
        ttk.Button(controls,text='关闭相机并恢复',command=lambda:self.send('camera-off')).pack(side='left',padx=6)
        ttk.Label(camera_tab,text='偏移沿初始机位右/上/前方向，单位为游戏世界单位。FOV=0 跟随轨道；倍率 <1 拉近、>1 变广（限 1–179°）。支持预设和播放中应用，停止恢复。',wraplength=650).pack(anchor='w',pady=6)
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

    def physics_controls(self):
        return validate_physics_controls({key:float(value.get()) for key,value in self.physics_values.items()})

    def reset_physics_controls(self):
        for key,value in PHYSICS_DEFAULTS.items(): self.physics_values[key].set(str(value))
        self.state.set('已恢复物理默认参数；恢复游戏动画后重新播放生效。')

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
            build=Path(self.build.get()); motion=Path(self.motion.get()) if self.motion.get().strip() else None; pid = int(self.pid.get())
            audio = Path(self.audio.get()) if self.audio.get().strip() else None
            stabilize_cloth=self.cloth_static.get()
            stabilize_braid=self.braid_static.get(); pause_bgm=self.pause_bgm.get()
            uid_text=validate_uid_text(self.uid_text.get()) if self.uid_enabled.get() else None
            camera_values=validate_camera_adjustments([float(v.get()) for v in self.camera_values],float(self.fov_multiplier.get()))
            fov_multiplier=float(self.fov_multiplier.get())
            hold_at_end=self.hold_at_end.get()
            audio_offset=float(self.audio_offset.get())
            manual_camera=not self.camera.get().strip() or self.camera_manual.get()
            camera = Path(self.camera.get()) if not manual_camera else None
        except Exception as exc: self.error(exc); return
        camera_enabled=True
        self.state.set('正在准备新会话…')
        def launch():
            directory = stage_session(build, motion, audio, ROOT / 'sessions', camera=camera, manual_camera=manual_camera,audio_offset=audio_offset,stabilize_cloth=stabilize_cloth,stabilize_braid=stabilize_braid,pause_bgm=pause_bgm,uid_text=uid_text,camera_values=camera_values if camera_enabled else None,fov_multiplier=fov_multiplier if camera_enabled else 1,hold_at_end=True,dynamic_session=True)
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
        if operation=='stop': self.preview_generation+=1; self.author_preview=None
        try:
            if not self.controller: raise ValueError('请先开始或连接会话')
            if self.controller.status and self.controller.status.get('dynamicSession'):
                if operation=='play' and self.controller.status.get('mmdPhase')==4: raise ValueError('正在恢复游戏动画，请稍候再播放')
                if operation=='stop': operation='restore'
                if operation=='camera-on': self.controller.prepare_camera(Path(self.camera.get()) if self.camera.get().strip() and not self.camera_manual.get() else None)
                if operation=='play' and self.controller.status.get('mmdPhase') not in (1,2):
                    if not self.motion.get().strip(): raise ValueError('请选择动作 VMD')
                    self.controller.prepare_physics(self.physics_enabled.get(),self.physics_controls() if self.physics_enabled.get() else None)
                    self.controller.prepare_face(self.face_enabled.get())
                    self.controller.load_motion(Path(self.motion.get()),Path(self.audio.get()) if self.audio.get().strip() else None,float(self.audio_offset.get()))
                    self.controller.prepare_front_camera(self.front_camera.get(),Path(self.camera.get()) if self.camera.get().strip() and not self.camera_manual.get() else None)
                    if self.front_camera.get():
                        for value in self.camera_values:value.set('0')
                        self.fov_multiplier.set('1')
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
        actor_status=self.controller.actor_status()
        if actor_status:
            actor_states={'ready':'可以生成角色','actor-created':'角色副本已创建，等待实机确认显示',
                'searching-loaded-model':'正在分批搜索已载入模型','loaded-model-not-found':'未找到可用模型或模板，可先打开该角色的预览','template-clone-failed':'模板生成失败，已尝试回滚','template-name-readback-failed':'模板模型名校验失败，已尝试回滚',
                'actor-models-valid':'演员网格、骨骼与材质有效','actor-models-incomplete':'演员网格、骨骼或材质异常','actors-none':'当前没有演员',
                'actors-cleared':'已移除演员','actor-limit-3':'最多生成三名额外演员',
                'source-not-found':'未找到来源模型','need-one-active-and-inactive-staging':'当前场景缺少可用的模型或非激活暂存节点',
                'restore-animation-before-spawn':'请先恢复游戏动画','clone-component-strip-refused':'副本组件检查失败，已尝试回滚',
                'clone-tree-refused':'副本骨架检查失败，已尝试回滚','profile-refused':'游戏接口校验失败',
                'rollback-failed-restart-game':'副本回滚失败，需要退出游戏清理',
                'packages-released':'演员已清理，资源引用已归还','package-release-incomplete':'资源归还未完成，请保留会话',
                'actor-template-ready':'角色模板已就绪',
                'sdk-resource-load-refused':'角色资源加载失败',
                'sdk-handles-released':'演员已清理，资源引用已归还',
                'sdk-release-pending':'资源归还未完成，请保留会话'}
            actor_states.update({'player-hidden':'当前角色已隐藏','player-shown':'当前角色已显示','player-visibility-pending':'角色显示恢复未完成，请保留会话','player-hide-pending':'正在隐藏角色','actor-camera-needs-manual':'请先在相机页启用手动相机','actor-camera-target-missing':'所选演员槽位或当前角色不可用','actor-camera-follow':'相机正在跟随所选演员','actor-camera-moved':'相机已移到所选演员','actor-camera-released':'已请求恢复相机'})
            actor_states.update({'actor-motion-playing':'演员动作播放中','actor-motion-paused':'演员动作已暂停','actor-motion-held':'播放完成，保持姿态',
                'actor-motion-restored':'演员姿态已恢复','actor-motion-idle':'演员动作未开始','actor-motion-restore-before-play':'请先恢复演员姿态再播放',
                'actor-motion-bones-refused':'动作与演员匹配的骨骼不足','actor-motion-source-or-slot-refused':'动作或槽位不可用，请先创建对应演员',
                'actor-motion-restore-failed':'演员姿态恢复失败，请保留会话'})
            count=sum(bool(slot.get('created')) for slot in actor_status.get('slots',[]))
            self.actor_result.set(f'额外演员：{count}/3 · '+actor_states.get(actor_status.get('status'),'操作未完成，请查看会话中的 actors.status.json'))
            if actor_status.get('dependencyCount'):
                self.actor_result.set(self.actor_result.get()+f"\n依赖项：{actor_status['dependencyCount']} · 本次补载：{actor_status.get('dependencyPackagesLoaded',0)} · 持有包：{actor_status.get('packageLeases',0)}")
            motion_status=actor_status.get('actorMotion',{})
            if motion_status.get('tracks'):
                self.actor_result.set(self.actor_result.get()+f"\n动作槽位：{motion_status.get('slot')} · {motion_status.get('seconds',0):.2f}/{motion_status.get('duration',0):.2f} 秒 · 骨骼轨道：{motion_status['tracks']}")
                self.actor_result.set(self.actor_result.get()+f" · 腰部辅助骨：{motion_status.get('pelvisHelpers',0)} · 根位移：{'开启' if motion_status.get('rootMotion') else '无源轨道'}")
            if actor_status.get('status') not in actor_states and actor_status.get('detail'):
                self.actor_result.set(self.actor_result.get()+f"\n{actor_status['status']} · {actor_status['detail']}")
            if actor_status.get('status')=='clone-component-strip-refused' and actor_status.get('detail'):
                self.actor_result.set(self.actor_result.get()+f"\n{actor_status.get('failedNode','')} / {actor_status.get('failedComponent','')} · {actor_status['detail']}")
            if actor_status.get('status')=='sdk-resource-load-refused':
                sdk_details={'sdk-handle-limit-or-disabled':'本会话加载次数已达上限，请结束并重新开启会话',
                    'sdk-model-not-catalogued':'该角色不在直接加载目录中',
                    'sdk-model-ambiguous':'角色名称匹配到多个模型',
                    'sdk-path-not-found':'游戏中未找到该角色的资源路径',
                    'sdk-loaded-source-identity-refused':'加载结果与所选角色不符',
                    'sdk-load-profile-refused':'游戏版本与加载接口校验不符'}
                self.actor_result.set(self.actor_result.get()+'\n'+sdk_details.get(actor_status.get('detail'),'请查看会话中的 actors.status.json'))
        if status:
            seconds, duration = status['seconds'], status['durationSeconds']
            self.clock.set(f'{seconds:08.3f} / {duration:08.3f} 秒')
            self.slider.configure(to=max(duration, .001))
            if not self.dragging: self.slider.set(seconds)
            self.loop.set(bool(status.get('loop')))
            states = ['等待可用角色场景（可停止）', '播放中', '已暂停', '播放结束']
            if status.get('dynamicSession') and status.get('mmdPhase')==0: states[0]='会话待命：可播放 MMD、开启相机或修改 UID'
            if status.get('dynamicSession') and status.get('mmdPhase')==3: states[0]='动作载入或绑定失败，可修正素材后重试'
            if status.get('dynamicSession') and status.get('mmdPhase')==4: states=['正在恢复游戏动画…']*4
            if status.get('holdingEnd'): states[2 if status.get('dynamicSession') else 3]='末帧保持中；恢复游戏动画后可继续使用会话' if status.get('dynamicSession') else '末帧保持中，点击停止并恢复结束'
            self.state.set('会话已结束' if status['closed'] else states[status['state']] if self.controller.fresh else '会话状态已过期，控制已停用')
        if status and self.controller.fresh and not status['closed']:
            music={1:'等待音乐接口',2:'音乐分类已暂停（场景覆盖待确认）',3:'音乐接口暂不可用；可在游戏设置中将音乐音量设为 0',5:'音乐接口异常'}.get(status.get('bgmState'))
            uid={1:'等待 UID 文字',2:'UID 文字已替换',3:'未找到受支持 UID 文字对象',5:'UID 写入异常'}.get(status.get('uidTextState'))
            if music: self.state.set(self.state.get()+'；'+music)
            if uid: self.state.set(self.state.get()+'；'+uid)
        camera_active=self.controller.fresh and bool(status and status.get('cameraActive' if status.get('dynamicSession') else 'cameraEnabled'))
        for button in [self.camera_apply,self.camera_reset]: button.state(['!disabled'] if camera_active else ['disabled'])
        try: self.controller.pump()
        except (OSError, ValueError) as exc: self.state.set('命令尚未发送：' + str(exc))
        loader_finished=self.loader and self.loader.poll() is not None
        if loader_finished or (status and status['closed']):
            if loader_finished and self.loader.returncode: self.state.set('加载失败，请查看会话目录中的 loader.log')
            if self.controller:
                reports = list(self.controller.directory.glob('gimmd-*.probe.json'))
                if reports:
                    try:
                        report = json.loads(reports[0].read_text(encoding='utf-8-sig'))
                        result = report.get('singleTrackPlayback', {})
                        camera = report.get('cameraPlayback', {})
                        if report.get('status')=='runtime-contract-mismatch':
                            self.state.set('会话启动校验失败。请完全退出游戏并重启，再刷新进程列表、选择新进程后连接。若仍失败，请检查游戏版本和所选构建。')
                        elif report.get('status')=='playback-cleanup-pending-hook-retained':
                            self.state.set('会话退出时未完成恢复。请完全退出游戏并重启，再连接新会话。')
                        elif result.get('status')=='multi-track-source-rejected':
                            reason=result.get('multiTrackPreview',{}).get('status','未知原因')
                            root=result.get('multiTrackPreview',{}).get('rootMotion',{}).get('status','')
                            if root=='rotated-root-controls-unsupported': reason='该旧构建不支持根旋转，请选择 build-studio-center-yaw'
                            self.state.set('动作无法加载：'+reason)
                        elif camera.get('cleanupPending') or camera.get('aborted'):
                            self.state.set('相机：' + camera.get('status', '恢复待确认'))
                        else: self.state.set('会话结束，姿态已恢复' if result.get('localRestored') and not result.get('cleanupPending') else
                                       '会话结束：' + result.get('status', report.get('status', '未知状态')))
                    except (OSError, ValueError): pass
        unsupported=self.imported_work.get('unsupported',{})
        if unsupported and '当前不播放' not in self.state.get():
            self.state.set(self.state.get()+'；当前不播放：'+', '.join(unsupported))
        if self.close_pending:
            if self.controller.fresh and not self.close_stop_sent:
                self.send('exit' if self.controller.status.get('dynamicSession') else 'stop'); self.close_stop_sent=True
            if (status and status['closed']) or (self.loader and self.loader.poll() is not None):
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
                values=validate_camera_adjustments([float(v.get()) for v in self.camera_values],float(self.fov_multiplier.get()))
                physics_controls=self.physics_controls()
                atomic_json(Path(value), {'schemaVersion': 1, 'build': self.build.get(), 'motion': self.motion.get(), 'audio': self.audio.get(), 'camera': self.camera.get(), 'cameraEnabled': self.camera_enabled.get(), 'manualCamera': self.camera_manual.get(),'frontCamera':self.front_camera.get(),'faceEnabled':self.face_enabled.get(),'physicsEnabled':self.physics_enabled.get(),'physicsControls':physics_controls, 'holdAtEnd':self.hold_at_end.get(),'cameraKeys':validate_keys(self.camera_keys) if self.camera_keys else [],'cameraValues':values,'fovMultiplier':float(self.fov_multiplier.get()),'stabilizeBraid':self.braid_static.get(),'pauseBgm':self.pause_bgm.get(),'uidEnabled':self.uid_enabled.get(),'uidText':validate_uid_text(self.uid_text.get()),'stabilizeCloth': self.cloth_static.get(), 'audioOffset': offset, 'importedWork': self.imported_work})
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
            if type(project.get('stabilizeCloth',False)) is not bool or type(project.get('cameraEnabled', False)) is not bool or type(project.get('manualCamera', False)) is not bool:
                raise ValueError('工程相机开关无效')
            if any(type(project.get(k,False)) is not bool for k in ['stabilizeBraid','pauseBgm','uidEnabled','holdAtEnd','frontCamera','faceEnabled','physicsEnabled']): raise ValueError('拍摄辅助开关无效')
            physics_controls=validate_physics_controls(project.get('physicsControls'))
            validate_uid_text(project.get('uidText','MMD 创作中'))
            camera_keys=project.get('cameraKeys',[])
            if camera_keys: camera_keys=validate_keys(camera_keys)
            elif not isinstance(camera_keys,list): raise ValueError('工程运镜关键帧无效')
            camera_values=validate_camera_adjustments(project.get('cameraValues',[0]*7),project.get('fovMultiplier',1))
            offset=project.get('audioOffset',0)
            if type(offset) not in (int,float) or not math.isfinite(offset) or abs(offset)>600:
                raise ValueError('工程音频偏移无效')
            if not isinstance(project.get('importedWork',{}),dict): raise ValueError('工程作品信息无效')
            for variable,value in zip(self.camera_values,camera_values): variable.set(str(value))
            self.camera_keys=camera_keys; self.refresh_camera_keys()
            self.hold_at_end.set(project.get('holdAtEnd',True))
            self.fov_multiplier.set(str(project.get('fovMultiplier',1)))
            self.audio_offset.set(str(offset)); self.imported_work=project.get('importedWork',{})
            self.camera_enabled.set(project.get('cameraEnabled', False))
            self.camera_manual.set(project.get('manualCamera', False))
            self.front_camera.set(project.get('frontCamera',True))
            self.face_enabled.set(project.get('faceEnabled',False))
            self.physics_enabled.set(project.get('physicsEnabled',False))
            for key,value in physics_controls.items(): self.physics_values[key].set(str(value))
            self.braid_static.set(project.get('stabilizeBraid',False)); self.pause_bgm.set(project.get('pauseBgm',False))
            self.uid_enabled.set(project.get('uidEnabled',False)); self.uid_text.set(project.get('uidText','MMD 创作中'))
            self.cloth_static.set(project.get('stabilizeCloth',False))
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
                self.state.set('相机 VMD 已导出并选中；关闭相机后重新开启即可载入。')
            except Exception as exc: self.error(exc)

    def apply_uid(self):
        try:
            if not self.controller: raise ValueError('请先开始会话')
            self.controller.uid(self.uid_text.get() if self.uid_enabled.get() else None)
            self.state.set('UID 调整已发送；取消勾选后应用可恢复原文字。')
        except Exception as exc: self.error(exc)

    def actor_command(self, operation):
        try:
            if not self.controller: raise ValueError('请先连接动态会话')
            source=self.actor_source.get().strip()
            source=ACTOR_CHOICES.get(source,source)
            if operation=='spawn-template' and source in ('当前角色','Ambor'):
                raise ValueError('该来源请使用“复制已载入模型”；直接加载请选择目录中的角色')
            self.controller.actors(operation,'' if source=='当前角色' or operation=='clear' else source)
            self.actor_result.set({'spawn':'正在生成角色…','spawn-template':'正在加载依赖并创建角色，首次加载可能短暂卡顿…','clear':'正在移除演员…','check':'正在检查演员…'}[operation])
        except Exception as exc: self.error(exc)

    def actor_shooting_command(self, operation):
        try:
            if not self.controller: raise ValueError('请先开启会话')
            self.controller.actors(operation,self.actor_motion_slot.get() if operation in ('camera-move','camera-follow') else '')
        except Exception as exc: self.error(exc)

    def actor_motion_command(self, operation):
        try:
            if not self.controller: raise ValueError('请先开启会话并创建演员')
            if operation=='motion-play':
                if not self.motion.get().strip(): raise ValueError('请先在播放页选择动作 VMD')
                self.controller.prepare_face(self.face_enabled.get())
                self.controller.prepare_actor_motion(Path(self.motion.get()))
            self.controller.actors(operation,self.actor_motion_slot.get() if operation=='motion-play' else '')
            self.actor_result.set('演员动作命令已发送…')
        except Exception as exc:self.error(exc)

    def author_preview_ready(self):
        if not self.controller or not self.controller.fresh or not self.controller.status.get('cameraActive' if self.controller.status.get('dynamicSession') else 'cameraEnabled'):
            raise ValueError('请先启用相机控制并开始会话；无相机 VMD 时可选择仅手动机位')
        return validate_keys(self.camera_keys)

    def preview_author_frame(self):
        try:
            keys=self.author_preview_ready(); seconds=float(self.key_time.get())
            self.controller.camera(sample_keys(keys,seconds),float(self.fov_multiplier.get()),preview_override=True)
            self.preview_generation+=1; self.author_preview=None
            self.state.set(f'已发送第 {seconds:.3f} 秒编排机位，直接在游戏中预览。')
        except Exception as exc: self.error(exc)

    def play_author_preview(self):
        try:
            keys=self.author_preview_ready(); start=float(self.key_time.get())
            if not math.isfinite(start) or not 0<=start<=keys[-1]['seconds']: raise ValueError('预览起点超出编排时长')
            multiplier=float(self.fov_multiplier.get()); validate_camera_adjustments(keys[0]['values'],multiplier)
            self.preview_generation+=1; generation=self.preview_generation
            self.author_preview=(keys,time.monotonic()-start,multiplier)
            self.author_preview_tick(generation)
        except Exception as exc: self.error(exc)

    def author_preview_tick(self,generation):
        if generation!=self.preview_generation or not self.author_preview: return
        try:
            keys,origin,multiplier=self.author_preview; seconds=min(time.monotonic()-origin,keys[-1]['seconds'])
            self.controller.camera(sample_keys(keys,seconds),multiplier,preview_override=True)
            if seconds>=keys[-1]['seconds']:
                self.author_preview=None; self.camera_result.set('运镜预览完成，保持编排末帧；点击返回会话机位退出预览。')
            else: self.root.after(33,lambda:self.author_preview_tick(generation))
        except Exception as exc:
            self.author_preview=None; self.error(exc)

    def stop_author_preview(self):
        self.preview_generation+=1; self.author_preview=None
        self.apply_camera()

    def apply_camera(self):
        self.preview_generation+=1; self.author_preview=None
        try:
            if not self.controller: raise ValueError('请先启用相机并开始会话')
            sequence=self.controller.camera([float(v.get()) for v in self.camera_values],float(self.fov_multiplier.get()))
            self.camera_result.set(f'相机命令 {sequence} 已发送，等待运行时应用。')
        except Exception as exc: self.error(exc)

    def reset_camera(self):
        for value in self.camera_values: value.set('0')
        self.fov_multiplier.set('1')
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
        self.preview_generation+=1; self.author_preview=None
        if self.controller and self.controller.status and self.controller.status['closed']:
            self.root.destroy(); return
        if self.working:
            self.error('正在准备会话，请等待准备结束'); return
        if (self.controller and self.controller.fresh) or (self.loader and self.loader.poll() is None):
            if self.controller and self.controller.fresh:
                self.send('exit' if self.controller.status.get('dynamicSession') else 'stop'); self.close_stop_sent=True
            self.close_pending = True; self.state.set('正在停止并等待恢复…')
        else: self.root.destroy()


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--session', type=Path); parser.add_argument('--project',type=Path); args = parser.parse_args()
    root = tk.Tk(); app = Studio(root)
    if args.session: app.controller = Controller(args.session)
    if args.project: app.load_project(args.project)
    root.mainloop()


if __name__ == '__main__': main()
