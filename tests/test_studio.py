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

    def test_tampered_build_is_rejected_before_execution(self):
        build=self.directory/'bad-build'; build.mkdir(); (build/'gimmd-probe.dll').write_bytes(b'wrong')
        atomic_json(build/'build-manifest.json',{'schemaVersion':1,'playerSession':True,'fullBodyFK':True,'dllSha256':'0'*64})
        with self.assertRaises(ValueError): stage_session(build,ROOT/'build-studio/multi-track.vmd',None,self.directory/'sessions')

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
