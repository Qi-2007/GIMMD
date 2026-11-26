import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/gui'))
from session import Controller,atomic_json,stage_session


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(dir=ROOT/'build-studio')
        self.directory=Path(self.temporary.name)
        self.status={'schemaVersion':2,'sessionToken':101,'closed':False,'durationSeconds':6,
                     'seconds':1,'state':1,'loop':False,'epoch':0,'commandSequence':0,'acknowledgedSequence':0}
        self.write(); self.controller=Controller(self.directory); self.controller.poll()

    def tearDown(self):
        import gc
        gc.collect()  # Destroy withdrawn Tk fixtures on their owning thread.
        assert self.directory.resolve().is_relative_to((ROOT/'build-studio').resolve())
        self.temporary.cleanup()

    def test_gaze_only_motion_rejected_before_session_or_asset_changes(self):
        from unittest.mock import patch
        from session import reject_gaze_only_motion
        summary={'boneTracks':1,'durationSeconds':27.2333,
                 'bones':[{'nameCp932Hex':'両目'.encode('cp932').hex()}]}
        with patch('session.run_tool',return_value=summary):
            with self.assertRaisesRegex(ValueError,'目线 VMD'):
                stage_session(ROOT/'build-studio',Path('gaze.vmd'),None,self.directory/'sessions')
            self.assertFalse((self.directory/'sessions').exists())
            self.status.update(dynamicSession=True,mmdPhase=0)
            self.write();self.controller.poll()
            for name in ('multi-track.vmd','multi-track.wav','actor-motion.vmd'):
                (self.directory/name).write_bytes(b'keep existing asset')
            with self.assertRaisesRegex(ValueError,'目线 VMD'):
                self.controller.load_motion(Path('gaze.vmd'))
            with self.assertRaisesRegex(ValueError,'目线 VMD'):
                self.controller.prepare_actor_motion(Path('gaze.vmd'))
            for name in ('multi-track.vmd','multi-track.wav','actor-motion.vmd'):
                self.assertEqual((self.directory/name).read_bytes(),b'keep existing asset')
        mixed={**summary,'boneTracks':2,'bones':summary['bones']+[{'nameCp932Hex':'センター'.encode('cp932').hex()}]}
        reject_gaze_only_motion(mixed)  # A normal motion may include ignored eye tracks.
    def write(self): atomic_json(self.directory/'player.status.json',self.status)

    def test_physics_opt_in_and_static_conflict(self):
        atomic_json(self.directory/'build-manifest.json',{})
        with self.assertRaisesRegex(ValueError,'不支持动态物理'):
            self.controller.prepare_physics(True)
        flag=self.directory/'physics-enabled.flag'
        self.assertFalse(flag.exists())
        atomic_json(self.directory/'build-manifest.json',{'secondaryPhysics':True})
        for name in ('cloth-static.flag','braid-static.flag'):
            (self.directory/name).write_text('1')
            with self.assertRaisesRegex(ValueError,'静态稳定冲突'):
                self.controller.prepare_physics(True)
            self.assertFalse(flag.exists()); (self.directory/name).unlink()
        self.controller.prepare_physics(True); self.assertTrue(flag.exists())
        self.controller.prepare_physics(False); self.assertFalse(flag.exists())

    def test_face_opt_in_requires_capability_and_preserves_disabled_flag_on_error(self):
        atomic_json(self.directory/'build-manifest.json',{})
        self.controller.prepare_face(False)
        flag=self.directory/'face-disabled.flag'
        self.assertTrue(flag.exists())
        with self.assertRaisesRegex(ValueError,'不支持表情'):
            self.controller.prepare_face(True)
        self.assertTrue(flag.exists())
        atomic_json(self.directory/'build-manifest.json',{'faceMotion':True})
        self.controller.prepare_face(True)
        self.assertFalse(flag.exists())
        self.controller.prepare_face(False)
        self.assertTrue(flag.exists())

    def test_physics_controls_validation_transport_and_active_guard(self):
        from session import PHYSICS_DEFAULTS, validate_physics_controls
        settings={**PHYSICS_DEFAULTS,'frontAngle':25,'rearResponseMs':220,'skirtSupportPercent':94}
        path=self.directory/'physics-controls.txt'
        atomic_json(self.directory/'build-manifest.json',{'secondaryPhysics':True})
        with self.assertRaisesRegex(ValueError,'不支持物理参数调整'): self.controller.prepare_physics(True,settings)
        self.assertFalse(path.exists())
        atomic_json(self.directory/'build-manifest.json',{'secondaryPhysics':True,'secondaryPhysicsControls':True})
        self.controller.prepare_physics(True,settings)
        self.assertEqual(path.read_text(),'GIMMD_PHYSICS_V1\n25 10 1.5 0.4 60 220 94\n')
        extended={**settings,'frontAngle':80}
        with self.assertRaisesRegex(ValueError,'45°'): self.controller.prepare_physics(True,extended)
        atomic_json(self.directory/'build-manifest.json',{'secondaryPhysics':True,'secondaryPhysicsControls':True,'extendedFrontAngle':True})
        self.controller.prepare_physics(True,extended)
        self.assertEqual(path.read_text(),'GIMMD_PHYSICS_V1\n80 10 1.5 0.4 60 220 94\n')
        both={**settings,'frontAngle':180,'rearAngle':180}
        with self.assertRaisesRegex(ValueError,'旧构建'): self.controller.prepare_physics(True,both)
        atomic_json(self.directory/'build-manifest.json',{'secondaryPhysics':True,'secondaryPhysicsControls':True,'fullSkirtAngleRange':True})
        self.controller.prepare_physics(True,both)
        self.assertEqual(path.read_text(),'GIMMD_PHYSICS_V1\n180 180 1.5 0.4 60 220 94\n')
        before=path.read_bytes()
        for invalid in [{'frontAngle':181},{'rearAngle':181},{'rearPaddingCm':-1},{'frontResponseMs':0},{'rearResponseMs':math.inf},{'skirtSupportPercent':True},{'frontAngle':math.nan},{'typo':1},[]]:
            with self.assertRaises(ValueError): self.controller.prepare_physics(True,invalid)
            self.assertEqual(path.read_bytes(),before)
        self.status.update(mmdPhase=1);self.write();self.controller.poll()
        with self.assertRaisesRegex(ValueError,'恢复游戏动画'): self.controller.prepare_physics(True,settings)
        self.assertEqual(path.read_bytes(),before)
        self.assertEqual(validate_physics_controls(),PHYSICS_DEFAULTS)

    def test_atomic_command_retries_transient_windows_reader(self):
        from unittest.mock import patch
        from session import atomic_text
        replace=os.replace;attempts=[]
        def interrupted(source,target):
            attempts.append(1)
            if len(attempts)<3:raise PermissionError('temporary command reader')
            return replace(source,target)
        with patch('session.os.replace',side_effect=interrupted),patch('session.time.sleep'):
            atomic_text(self.directory/'actors.command','1 motion-play 1')
        self.assertEqual((self.directory/'actors.command').read_text(),'1 motion-play 1')
        self.assertEqual(len(attempts),3)

    def test_actor_motion_requires_capability_and_valid_slot(self):
        self.status['dynamicSession']=True;self.write();self.controller.poll()
        atomic_json(self.directory/'build-manifest.json',{'localActors':True})
        with self.assertRaises(ValueError):self.controller.actors('motion-play','1')
        atomic_json(self.directory/'build-manifest.json',{'localActors':True,'actorMotion':True})
        with self.assertRaises(ValueError):self.controller.actors('motion-play','4')
        self.assertEqual(self.controller.actors('motion-play','2'),1)
        self.assertEqual((self.directory/'actors.command').read_text(),'1 motion-play 2')

    def test_commands_wait_for_acknowledgement(self):
        c=self.controller; c.enqueue('pause'); self.assertEqual(c.pump(),'1 pause')
        c.enqueue('play'); self.assertIsNone(c.pump())
        self.status.update(acknowledgedSequence=1,commandSequence=1); self.write(); c.poll()
        self.assertEqual(c.pump(),'2 play')

    def test_front_camera_can_recenter_active_camera_and_rejects_old_build(self):
        self.status.update(dynamicSession=True,cameraEnabled=True,cameraActive=True)
        self.write();self.controller.poll()
        atomic_json(self.directory/'build-manifest.json',{'cameraFront':True})
        self.controller.prepare_front_camera(True)
        self.assertEqual((self.directory/'camera-front.flag').read_text(),'1')
        self.assertEqual((self.directory/'camera.command').read_text().split()[1:],['0']*7)
        self.controller.prepare_front_camera(False)
        self.assertFalse((self.directory/'camera-front.flag').exists())
        atomic_json(self.directory/'build-manifest.json',{})
        with self.assertRaises(ValueError):self.controller.prepare_front_camera(True)
        self.assertFalse((self.directory/'camera-front.flag').exists())

    def test_actor_commands_wait_for_creation_and_require_supported_session(self):
        c=self.controller
        self.status.update(dynamicSession=True,mmdPhase=0); self.write(); c.poll()
        atomic_json(self.directory/'build-manifest.json',{'localActors':True})
        self.assertEqual(c.actors('spawn','Ambor'),1)
        self.assertEqual((self.directory/'actors.command').read_text(),'1 spawn Ambor')
        with self.assertRaises(ValueError): c.actors('spawn')
        atomic_json(self.directory/'actors.status.json',{'schemaVersion':1,'acknowledgedSequence':1,'slots':[]})
        self.assertEqual(c.actors('clear'),2)
        with self.assertRaises(ValueError): c.actors('spawn','Ambor\nclear')
        self.status['mmdPhase']=2; self.write(); c.poll()
        with self.assertRaises(ValueError): c.actors('spawn')

    def test_actor_inspection_rejects_legacy_build_without_queuing(self):
        self.status.update(dynamicSession=True,mmdPhase=0); self.write(); self.controller.poll()
        atomic_json(self.directory/'build-manifest.json',{'localActors':True})
        with self.assertRaises(ValueError): self.controller.actors('check')
        self.assertFalse((self.directory/'actors.command').exists())
        atomic_json(self.directory/'build-manifest.json',{'localActors':True,'actorInspection':True})
        self.assertEqual(self.controller.actors('check'),1)
        self.assertEqual((self.directory/'actors.command').read_text(),'1 check')

    def test_template_operations_require_experimental_capability(self):
        self.status.update(dynamicSession=True,mmdPhase=0); self.write(); self.controller.poll()
        atomic_json(self.directory/'build-manifest.json',{'localActors':True})
        for operation in ('spawn-template','load-template'):
            with self.assertRaises(ValueError): self.controller.actors(operation,'Columbina')
        self.assertFalse((self.directory/'actors.command').exists())
        atomic_json(self.directory/'build-manifest.json',{'localActors':True,'actorTemplateLoading':True})
        with self.assertRaises(ValueError): self.controller.actors('load-template')
        self.assertEqual(self.controller.actors('load-template','Columbina'),1)
        self.assertEqual((self.directory/'actors.command').read_text(),'1 load-template Columbina')

    def test_old_build_rejects_actor_commands(self):
        self.status.update(dynamicSession=True,mmdPhase=0); self.write(); self.controller.poll()
        atomic_json(self.directory/'build-manifest.json',{'localActors':False})
        with self.assertRaises(ValueError): self.controller.actors('spawn')

    def test_stop_overrides_unacknowledged_queue(self):
        c=self.controller; c.enqueue('pause'); c.pump(); c.enqueue('play'); c.enqueue('stop')
        self.assertEqual(c.pump(),'2 stop'); self.assertFalse(c.pending)

    def test_seek_is_bounded_and_coalesced(self):
        c=self.controller
        for value in [-1,math.nan,math.inf,7]:
            with self.assertRaises(ValueError): c.enqueue('seek',value)
        c.enqueue('seek',2); c.enqueue('seek',3)
        self.assertEqual(c.pump(),'1 seek 3.000')

    def test_finished_and_stale_sessions_disable_control(self):
        c=self.controller; self.status['closed']=True; self.write(); c.poll()
        with self.assertRaises(ValueError): c.enqueue('play')
        self.status['closed']=False; self.write(); os.utime(self.directory/'player.status.json',(time.time()-10,)*2); c.poll()
        self.assertFalse(c.fresh)

    def test_reopening_does_not_reuse_pending_sequence(self):
        (self.directory/'player.command').write_text('9 pause')
        c=Controller(self.directory); c.poll(); c.enqueue('play'); self.assertIsNone(c.pump())
        self.status.update(acknowledgedSequence=9,commandSequence=9); self.write(); c.poll()
        self.assertEqual(c.pump(),'10 play')

    def test_bad_status_does_not_send_commands(self):
        c=self.controller; (self.directory/'player.status.json').write_text('{')
        self.assertIsNone(c.poll()); self.assertFalse(c.fresh)
        self.status['seconds']=float('nan'); self.write(); self.assertIsNone(c.poll())

    def test_new_token_discards_old_commands(self):
        c=self.controller; c.enqueue('pause'); self.status['sessionToken']=202; self.write(); c.poll()
        self.assertFalse(c.pending); self.assertIsNone(c.pump())

    def test_unique_staging_keeps_sources_unchanged(self):
        build=ROOT/'build-studio'; source=build/'multi-track.vmd'; original=source.read_bytes()
        a=stage_session(build,source,build/'multi-track.wav',self.directory)
        b=stage_session(build,source,None,self.directory)
        self.assertNotEqual(a,b); self.assertEqual(source.read_bytes(),original)
        self.assertEqual((a/'multi-track.vmd').read_bytes(),original)
        self.assertFalse((b/'multi-track.wav').exists()); self.assertFalse((a/'player.command').exists())

    def test_shooting_staging_and_validation(self):
        from session import validate_uid_text
        for invalid in ['x'*65,'bad\nline','bad\0value',42]:
            with self.assertRaises(ValueError): validate_uid_text(invalid)
        build=ROOT/'build-studio-shooting'
        directory=stage_session(build,ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',
            stabilize_braid=True,pause_bgm=True,uid_text='MMD 创作中')
        self.assertTrue((directory/'braid-static.flag').exists())
        self.assertTrue((directory/'pause-bgm.flag').exists())
        self.assertEqual((directory/'uid-text.txt').read_text(encoding='utf-8'),'MMD 创作中')
        with self.assertRaises(ValueError):
            stage_session(ROOT/'build-studio',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',pause_bgm=True)

    def test_tampered_build_is_rejected_before_execution(self):
        build=self.directory/'bad-build'; build.mkdir(); (build/'gimmd-probe.dll').write_bytes(b'wrong')
        atomic_json(build/'build-manifest.json',{'schemaVersion':1,'playerSession':True,'fullBodyFK':True,'dllSha256':'0'*64})
        with self.assertRaises(ValueError): stage_session(build,ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions')

    def test_camera_staging_and_unsupported_build(self):
        camera=ROOT/'build-studio/camera-fixture.vmd'
        with self.assertRaises(ValueError):
            stage_session(ROOT/'build-studio',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',camera=camera)
        directory=stage_session(ROOT/'build-camera-player',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',camera=camera)
        self.assertEqual((directory/'camera.vmd').read_bytes(),camera.read_bytes())
        self.assertEqual(json.loads((directory/'session.json').read_text())['camera'],str(camera.resolve()))
        data=bytearray(camera.read_bytes()); data[50+4+4+4+60]=1
        invalid=self.directory/'orthographic.vmd'; invalid.write_bytes(data)
        with self.assertRaises(ValueError):
            stage_session(ROOT/'build-camera-player',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',camera=invalid)

    def test_manual_camera_transport_and_staging(self):
        self.status.update(cameraEnabled=True,cameraAcknowledgedSequence=0); self.write(); self.controller.poll()
        self.assertEqual(self.controller.camera([1,2,3,90,0,0,60]),1)
        self.assertEqual(self.controller.camera([0]*7),2)
        self.assertEqual((self.directory/'camera.command').read_text(),'2 0 0 0 0 0 0 0')
        for values in [[math.nan]*7,[0,0,0,181,0,0,0],[0,0,0,0,0,0,180]]:
            with self.assertRaises(ValueError): self.controller.camera(values)
        self.status['cameraEnabled']=False; self.write(); self.controller.poll()
        with self.assertRaises(ValueError): self.controller.camera([0]*7)
        directory=stage_session(ROOT/'build-studio-camera-fov',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',manual_camera=True)
        self.assertTrue((directory/'manual-camera.flag').exists()); self.assertFalse((directory/'camera.vmd').exists())
        self.assertTrue(json.loads((directory/'session.json').read_text())['manualCamera'])

    def test_camera_adjustments_staging_transport_and_project(self):
        from session import validate_camera_adjustments
        from unittest.mock import patch
        import tkinter as tk
        from player_gui import Studio
        for multiplier in [0,4.1,math.nan,True]:
            with self.assertRaises(ValueError): validate_camera_adjustments([0]*7,multiplier)
        directory=stage_session(ROOT/'build-studio-camera-adjust',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',
            camera=ROOT/'build-studio/camera-fixture.vmd',camera_values=[1,2,3,0,0,0,0],fov_multiplier=.75)
        self.assertEqual((directory/'camera-initial.txt').read_text(),'1 1 2 3 0 0 0 0 0.75')
        self.status.update(cameraEnabled=True,cameraAcknowledgedSequence=0); atomic_json(directory/'player.status.json',self.status)
        controller=Controller(directory); controller.poll(); controller.camera([0]*7,.5)
        self.assertEqual((directory/'camera.command').read_text(),'1 0 0 0 0 0 0 0 0.5')
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.camera_values[0].set('2'); app.fov_multiplier.set('.75'); app.hold_at_end.set(True)
            self.assertFalse(app.face_enabled.get()); app.face_enabled.set(True)
            self.assertFalse(app.physics_enabled.get()); app.physics_enabled.set(True)
            app.physics_values['frontAngle'].set('135');app.physics_values['rearAngle'].set('155');app.physics_values['rearResponseMs'].set('220')
            path=self.directory/'adjust-project.json'
            with patch('player_gui.filedialog.asksaveasfilename',return_value=str(path)): app.save_project()
            app.face_enabled.set(False)
            app.physics_enabled.set(False)
            self.assertFalse(app.last_error); app.camera_values[0].set('0'); app.fov_multiplier.set('1'); app.hold_at_end.set(False); app.load_project(path)
            self.assertTrue(app.hold_at_end.get())
            self.assertTrue(app.face_enabled.get())
            self.assertTrue(app.physics_enabled.get())
            self.assertEqual(app.physics_controls()['frontAngle'],135);self.assertEqual(app.physics_controls()['rearAngle'],155)
            self.assertEqual(app.physics_controls()['rearResponseMs'],220)
            app.reset_physics_controls();self.assertEqual(app.physics_controls()['frontAngle'],24)
            self.assertEqual(app.physics_controls()['rearResponseMs'],160)
            self.assertEqual(float(app.camera_values[0].get()),2); self.assertEqual(float(app.fov_multiplier.get()),.75)
            project=json.loads(path.read_text(encoding='utf-8')); project['fovMultiplier']=0; atomic_json(path,project); app.load_project(path)
            self.assertEqual(float(app.fov_multiplier.get()),.75); self.assertTrue(app.last_error)
        finally: root.destroy()

    def test_live_author_preview_and_uid_without_vmd_export(self):
        from camera_authoring import sample_keys
        keys=[{'seconds':0,'values':[0,0,0,0,0,0,40]}, {'seconds':2,'values':[2,4,6,20,10,0,60]}]
        self.assertEqual(sample_keys(keys,1),[1,2,3,10,5,0,50])
        self.status.update(cameraEnabled=True,cameraAcknowledgedSequence=0,state=3,holdingEnd=True); self.write(); self.controller.poll()
        manifest=json.loads((ROOT/'build-studio-live/build-manifest.json').read_text(encoding='utf-8-sig')); atomic_json(self.directory/'build-manifest.json',manifest)
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.controller=self.controller; app.camera_keys=keys; app.key_time.set('1'); app.preview_author_frame()
            self.assertFalse(app.last_error); self.assertEqual((self.directory/'camera.command').read_text(),'1 1 2 3 10 5 0 50 1 1')
            self.assertFalse(list(self.directory.glob('*.vmd')))
            app.uid_enabled.set(True); app.uid_text.set('预览文字'); app.apply_uid()
            self.assertEqual((self.directory/'uid-command.txt').read_text(encoding='utf-8'),'1 预览文字')
            app.uid_enabled.set(False); app.apply_uid(); self.assertEqual((self.directory/'uid-command.txt').read_text(),'0')
            app.stop_author_preview(); self.assertEqual((self.directory/'camera.command').read_text(),'2 0 0 0 0 0 0 0')
            self.assertTrue(app.hold_at_end.get())
        finally: root.destroy()

    def test_dynamic_standby_and_independent_commands(self):
        directory=stage_session(ROOT/'build-studio-dynamic',None,None,self.directory/'sessions',manual_camera=True,dynamic_session=True)
        self.assertTrue((directory/'dynamic-session.flag').exists()); self.assertFalse((directory/'multi-track.vmd').exists())
        self.status.update(dynamicSession=True,mmdPhase=0,cameraEnabled=True,cameraActive=False); atomic_json(directory/'player.status.json',self.status)
        controller=Controller(directory); controller.poll(); controller.load_motion(ROOT/'build-studio/multi-track.vmd')
        self.assertTrue((directory/'multi-track.vmd').exists())
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.controller=controller
            for sequence,operation,expected in [(1,'stop','restore'),(2,'camera-on','camera-on'),(3,'camera-off','camera-off'),(4,'exit','exit')]:
                app.send(operation); self.assertFalse(app.last_error)
                self.assertEqual((directory/'player.command').read_text(),f'{sequence} {expected}')
                self.status.update(acknowledgedSequence=sequence,commandSequence=sequence); atomic_json(directory/'player.status.json',self.status); controller.poll()
        finally: root.destroy()

    def test_hold_end_session_flag_and_status(self):
        directory=stage_session(ROOT/'build-studio-hold-end',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',hold_at_end=True)
        self.assertTrue((directory/'hold-end.flag').exists())
        self.assertTrue(json.loads((directory/'session.json').read_text())['holdAtEnd'])
        with self.assertRaises(ValueError):
            stage_session(ROOT/'build-studio-camera-adjust',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',hold_at_end=True)
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            self.status.update(state=3,holdingEnd=True); self.write()
            app=Studio(root,test=True); app.controller=self.controller; app.poll()
            self.assertIn('末帧保持中',app.state.get()); self.assertFalse(app.buttons['stop'].instate(['disabled']))
            app.buttons['stop'].invoke(); self.assertEqual((self.directory/'player.command').read_text(),'1 stop')
        finally: root.destroy()

    def test_camera_key_authoring_export_roundtrip(self):
        import tkinter as tk
        from player_gui import Studio
        from camera_authoring import encode_vmd,load_keys,validate_keys
        from session import run_tool
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.record_camera_key()
            self.assertEqual(app.camera_keys[0]['values'][6],45)
            app.key_time.set('1'); app.camera_values[0].set('2'); app.camera_values[3].set('90'); app.camera_values[6].set('60')
            app.record_camera_key(); self.assertEqual(len(app.camera_keys),2)
            path=self.directory/'authored-camera.vmd'; path.write_bytes(encode_vmd(app.camera_keys))
            sample=run_tool([ROOT/'build-studio-camera-fov/gimmd-camera.exe',path,'.5'])
            self.assertAlmostEqual(sample['position'][0],-1,places=5); self.assertAlmostEqual(sample['fov'],52.5)
            project=self.directory/'camera-keys.json'; atomic_json(project,{'schemaVersion':1,'type':'gimmd-camera-keys','keys':app.camera_keys})
            self.assertEqual(load_keys(project),app.camera_keys)
            with self.assertRaises(ValueError): validate_keys([app.camera_keys[0],app.camera_keys[0]])
            app.key_list.selection_set('1'); app.recall_camera_key(); self.assertEqual(float(app.camera_values[0].get()),2)
            app.delete_camera_key(); self.assertEqual(len(app.camera_keys),1); self.assertFalse(app.last_error)
        finally: root.destroy()

    def test_work_import_validates_paths_and_preserves_unsupported_tracks(self):
        from work_import import import_work
        for name in ['motion.vmd','camera.vmd','face.vmd','music.wav','second.vmd']:
            (self.directory/name).write_bytes(b'fixture')
        path=self.directory/'set.ini'
        path.write_text('[set]\nname=测试作品\nmotion=motion.vmd\ncamera=camera.vmd\nface=face.vmd\nmotion2=second.vmd\nmusic=music.wav\naudio_offset=-.25\n',encoding='utf-8-sig')
        work=import_work(path)
        self.assertEqual(work['audioOffset'],-.25); self.assertEqual(set(work['unsupported']),{'face','motion2'})
        for invalid in ['motion=../motion.vmd','motion=motion.vmd\naudio_offset=nan','motion=missing.vmd','motion=motion.vmd\nmotion=motion.vmd']:
            path.write_text(invalid,encoding='utf-8')
            with self.assertRaises(ValueError): import_work(path)

    def test_audio_offset_stages_pcm_without_changing_original(self):
        import wave
        source=self.directory/'source.wav'; data=bytes(range(64))
        with wave.open(str(source),'wb') as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(8000); output.writeframes(data)
        original=source.read_bytes()
        for offset,expected in [(.001,data[16:]),(-.001,b'\0'*16+data)]:
            directory=stage_session(ROOT/'build-studio-camera-fov',ROOT/'build-studio/multi-track.vmd',source,self.directory/'sessions',audio_offset=offset)
            with wave.open(str(directory/'multi-track.wav'),'rb') as stream:
                self.assertEqual(stream.readframes(stream.getnframes()),expected)
            self.assertEqual(json.loads((directory/'session.json').read_text())['audioOffset'],offset)
        self.assertEqual(source.read_bytes(),original)

    def test_cloth_static_session_flag_and_build_capability(self):
        with self.assertRaises(ValueError):
            stage_session(ROOT/'build-studio-camera-fov',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',stabilize_cloth=True)
        directory=stage_session(ROOT/'build-studio-cloth-static',ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions',stabilize_cloth=True)
        self.assertEqual((directory/'cloth-static.flag').read_text(),'1')
        self.assertTrue(json.loads((directory/'session.json').read_text())['stabilizeCloth'])

    def test_real_buttons_and_layout(self):
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.controller=self.controller; app.poll()
            app.buttons['pause'].invoke()
            self.assertEqual((self.directory/'player.command').read_text(),'1 pause')
            self.status.update(acknowledgedSequence=1,commandSequence=1,state=2); self.write(); app.poll()
            app.position.set('2.5'); app.seek.invoke()
            self.assertEqual((self.directory/'player.command').read_text(),'2 seek 2.500')
            self.status['closed']=True; self.write(); app.poll()
            self.assertTrue(app.buttons['play'].instate(['disabled']))
            root.update_idletasks()
            self.assertLessEqual(root.winfo_reqheight(),690)
            self.assertFalse(app.last_error)
        finally: root.destroy()

    def test_closed_session_reports_startup_and_cleanup_failures(self):
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.controller=Controller(self.directory)
            self.status.update(closed=True,state=0); self.write()
            for failure,expected in [('runtime-contract-mismatch','启动校验失败'),
                                     ('playback-cleanup-pending-hook-retained','未完成恢复')]:
                atomic_json(self.directory/'gimmd-100.probe.json',{'status':failure,
                            'singleTrackPlayback':{'status':'waiting-for-fixture'}})
                app.poll()
                self.assertIn(expected,app.state.get())
        finally: root.destroy()

    def test_close_during_startup_waits_then_sends_stop(self):
        import tkinter as tk
        from player_gui import Studio
        class StartingLoader:
            def poll(self): return None
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True); app.controller=Controller(self.directory); app.loader=StartingLoader()
            app.close(); self.assertTrue(app.close_pending); self.assertFalse(app.close_stop_sent)
            app.poll()
            self.assertTrue(app.close_stop_sent)
            self.assertEqual((self.directory/'player.command').read_text(),'1 stop')
        finally: root.destroy()

    def test_project_load_is_data_only_and_atomic(self):
        import tkinter as tk
        from player_gui import Studio
        root=tk.Tk(); root.withdraw()
        try:
            app=Studio(root,test=True)
            path=self.directory/'project.json'
            atomic_json(path,{'schemaVersion':1,'build':'valid build','motion':"测试/It's me.vmd",'audio':'sound.wav','camera':'camera.vmd'})
            app.load_project(path)
            self.assertEqual(app.motion.get(),"测试/It's me.vmd"); self.assertIsNone(app.controller); self.assertIsNone(app.loader)
            atomic_json(path,{'schemaVersion':1,'build':'must not apply','camera':42})
            app.load_project(path)
            self.assertEqual(app.build.get(),'valid build'); self.assertTrue(app.last_error)
        finally: root.destroy()


if __name__=='__main__': unittest.main(verbosity=2)
