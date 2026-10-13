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
    def write(self): atomic_json(self.directory/'player.status.json',self.status)

    def test_commands_wait_for_acknowledgement(self):
        c=self.controller; c.enqueue('pause'); self.assertEqual(c.pump(),'1 pause')
        c.enqueue('play'); self.assertIsNone(c.pump())
        self.status.update(acknowledgedSequence=1,commandSequence=1); self.write(); c.poll()
        self.assertEqual(c.pump(),'2 play')

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
            path=self.directory/'adjust-project.json'
            with patch('player_gui.filedialog.asksaveasfilename',return_value=str(path)): app.save_project()
            self.assertFalse(app.last_error); app.camera_values[0].set('0'); app.fov_multiplier.set('1'); app.hold_at_end.set(False); app.load_project(path)
            self.assertTrue(app.hold_at_end.get())
            self.assertEqual(float(app.camera_values[0].get()),2); self.assertEqual(float(app.fov_multiplier.get()),.75)
            project=json.loads(path.read_text(encoding='utf-8')); project['fovMultiplier']=0; atomic_json(path,project); app.load_project(path)
            self.assertEqual(float(app.fov_multiplier.get()),.75); self.assertTrue(app.last_error)
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
