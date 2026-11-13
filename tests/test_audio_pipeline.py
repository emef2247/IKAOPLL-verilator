#!/usr/bin/env python3
"""End-to-end checks using generated, redistributable OPLL tones (no game data).
Usage: python3 tests/test_audio_pipeline.py build/obj_dir/ikaopll_sim
"""
import csv
import gzip
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from array import array

SIM = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else Path('build/obj_dir/ikaopll_sim').resolve()
CLOCK = 3579545

def write_vgm(path, commands, samples, clock=CLOCK):
    data = bytearray(0x100)
    data[:4] = b'Vgm '
    struct.pack_into('<I', data, 8, 0x170)
    struct.pack_into('<I', data, 0x10, clock)
    struct.pack_into('<I', data, 0x18, samples)
    struct.pack_into('<I', data, 0x34, 0x100-0x34)
    data.extend(commands + b'\x66')
    struct.pack_into('<I', data, 4, len(data)-4)
    path.write_bytes(data)

def wait(n):
    result = b''
    while n:
        step = min(n, 65535)
        result += b'\x61' + struct.pack('<H', step)
        n -= step
    return result

def reg(addr, value):
    return bytes((0x51, addr, value))

def tone_setup(fnum=288, block=4):
    # User patch: sustained carrier, modulator at maximum attenuation.
    patch = [0x21, 0x21, 0x3f, 0, 0xf0, 0xf0, 0, 0]
    return b''.join(reg(i, v) for i,v in enumerate(patch)) + reg(0x30,0) + reg(0x10,fnum&255) + reg(0x20,0x10|(block<<1)|(fnum>>8))

def read_wav(path):
    with wave.open(str(path),'rb') as wav:
        assert wav.getnchannels() == 1 and wav.getsampwidth() == 2
        rate = wav.getframerate()
        values = array('h',wav.readframes(wav.getnframes()))
        if sys.byteorder != 'little': values.byteswap()
        return rate, values

def read_csv(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))

def frequency(values, rate):
    values = values[rate//5:rate*4//5]
    center = sum(values)/len(values)
    crossing = []
    for i in range(1,len(values)):
        a,b = values[i-1]-center, values[i]-center
        if a <= 0 < b: crossing.append(i-1-a/(b-a))
    if len(crossing) < 20: raise AssertionError('Too few tone cycles')
    return (len(crossing)-1)*rate/(crossing[-1]-crossing[0])

class AudioPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='ikaopll-tests-')
        cls.root = Path(cls.temp.name)
    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()
    def render(self, name, commands, samples, *, clock=CLOCK, rate=44100, diagnostics=False):
        source = self.root/(name+'.vgm')
        output = self.root/(name+'.wav')
        write_vgm(source,commands,samples,clock)
        args = [str(SIM),str(source),'--output',str(output),'--sample-rate',str(rate)]
        if diagnostics: args += ['--enable-csv','--debug',str(self.root/(name+'.bus.csv'))]
        run = subprocess.run(args,cwd=self.root,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        actual_rate, values = read_wav(output)
        self.assertEqual(actual_rate,rate)
        # A VGM deadline rounds up to a physical master-clock edge. This can
        # add one output frame relative to flooring the ideal VGM duration.
        clocks = (samples*clock+44099)//44100
        self.assertEqual(len(values),clocks*rate//clock)
        return source,output,values,run.stdout
    def test_pitch_sign_strobe_and_bus_spacing(self):
        _,_,values,_ = self.render('tone',tone_setup()+wait(44100),44100,diagnostics=True)
        expected = CLOCK*288*8/(72*2**18)
        self.assertAlmostEqual(frequency(values,44100),expected,delta=0.5)
        self.assertLess(min(values),-100)
        self.assertGreater(max(values),100)
        raw = read_csv(self.root/'tone.acc.csv')
        clocks = [int(row['clock']) for row in raw]
        self.assertGreater(len(clocks),49000)
        self.assertEqual(set(b-a for a,b in zip(clocks,clocks[1:])),{72})
        bus = read_csv(self.root/'tone.bus.csv')
        for previous,current in zip(bus,bus[1:]):
            minimum = 12 if previous['op']=='ADDR' else 84
            self.assertGreaterEqual(int(current['actual_clock'])-int(previous['actual_clock']),minimum)
        # Audio collection continues during the initial write burst.
        self.assertLess(clocks[0],72)
        self.assertEqual(len(read_csv(self.root/'tone.audio.csv')),44100)
    def test_48000_is_resampled(self):
        _,_,values,_ = self.render('48k',tone_setup()+wait(44100),44100,rate=48000)
        self.assertAlmostEqual(frequency(values,48000),CLOCK*288*8/(72*2**18),delta=0.5)
        _,_,short,_ = self.render('short48k',wait(113),113,rate=48000)
        self.assertEqual(len(short),123)
    def test_header_clock_controls_pitch(self):
        _,_,values,_ = self.render('4mhz',tone_setup()+wait(44100),44100,clock=4000000)
        self.assertAlmostEqual(frequency(values,44100),4000000*288*8/(72*2**18),delta=0.5)
    def test_silence_and_single_sample_tail(self):
        _,_,values,_ = self.render('silence',wait(44100),44100)
        self.assertTrue(all(v==0 for v in values))
        self.render('tiny',wait(1),1)
    def test_trailing_multiple_waits(self):
        self.render('tail',tone_setup()+wait(22050)+reg(0x20,9)+wait(11025)+wait(11025),44100)
    def test_dense_writes_keep_absolute_time(self):
        _,_,sparse,_ = self.render('sparse',tone_setup()+wait(44100),44100)
        commands = tone_setup()
        for _ in range(100): commands += wait(441)+reg(0x30,0)
        _,_,dense,_ = self.render('dense',commands,44100)
        self.assertEqual(sparse,dense)
    def test_render_repeatability(self):
        source,output,values,_ = self.render('roundtrip',tone_setup()+wait(20000)+reg(0x20,9)+wait(24100),44100,diagnostics=True)
        expected = output.read_bytes()
        run = subprocess.run([str(SIM),str(source),'--output',str(self.root/'repeated.wav')],cwd=self.root,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(expected,(self.root/'repeated.wav').read_bytes())
    def test_rhythm_output(self):
        commands = reg(0x36,0)+reg(0x37,0)+reg(0x38,0)
        commands += reg(0x16,0x20)+reg(0x17,0x50)+reg(0x18,0xc0)
        commands += reg(0x26,5)+reg(0x27,5)+reg(0x28,1)+reg(0x0e,0x3f)+wait(44100)
        _,_,values,_ = self.render('rhythm',commands,44100)
        self.assertGreater(max(abs(v) for v in values),100)
    def test_bad_inputs_and_unwritable_output_fail(self):
        for name,body in [('truncated',b'\x51\x20'),('unknown',b'\xff')]:
            path = self.root/(name+'.vgm'); write_vgm(path,body,0)
            run = subprocess.run([str(SIM),str(path),'--output',str(self.root/(name+'.wav'))],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
        source = self.root/'silence_for_error.vgm'; write_vgm(source,wait(100),100)
        run = subprocess.run([str(SIM),str(source),'--output',str(self.root/'absent'/'bad.wav')],capture_output=True,text=True)
        self.assertNotEqual(run.returncode,0)

    def test_output_aliases_never_overwrite_input(self):
        source = self.root/'protected.vgm'
        write_vgm(source,wait(100),100)
        expected = source.read_bytes()
        alias = self.root/'protected_alias.vgm'
        alias.symlink_to(source)
        for args in ([str(source),'--debug',str(source)],
                     [str(source),'--vcd',str(alias)],
                     [str(source),'--output',str(self.root/'./protected.vgm')],
                     [str(source),'--output',str(alias)]):
            run = subprocess.run([str(SIM)]+args,cwd=self.root,capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            self.assertEqual(source.read_bytes(),expected)

    def test_gzip_vgm_and_vgz_match_plain_input(self):
        source,output,_,_ = self.render('gzip_reference',tone_setup()+wait(44100),44100)
        expected = output.read_bytes()
        for extension in ('.vgm','.vgz'):
            packed = self.root/('compressed'+extension)
            packed.write_bytes(gzip.compress(source.read_bytes(),mtime=0))
            result = self.root/'gzip_result.wav'
            run = subprocess.run([str(SIM),str(packed),'--output',str(result)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertEqual(result.read_bytes(),expected)
        valid_packed = packed.read_bytes()
        bad_crc = bytearray(valid_packed)
        bad_crc[-8] ^= 1
        packed.write_bytes(bad_crc)
        run = subprocess.run([str(SIM),str(packed),'--output',str(result)],capture_output=True,text=True)
        self.assertNotEqual(run.returncode,0)
        packed.write_bytes(valid_packed[:-6])
        run = subprocess.run([str(SIM),str(packed),'--output',str(result)],capture_output=True,text=True)
        self.assertNotEqual(run.returncode,0)

if __name__ == '__main__': unittest.main(verbosity=2)
