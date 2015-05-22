#!/usr/bin/env pypy

import math
import os
import weakref

# -- rpython imports and platform override --

from rpython.rtyper.lltypesystem import rffi
from rpython.rtyper.lltypesystem import lltype
from rpython.translator.tool.cbuild import ExternalCompilationInfo
from rpython.translator.platform.darwin import Darwin
from rpython.translator import platform

class Darwin_x86_64(Darwin):
    name = "darwin_x86_64"
    link_flags = ('-arch', 'x86_64', '-mmacosx-version-min=10.10', '-stdlib=libc++')
    cflags = ('-arch', 'x86_64', '-O3', '-fomit-frame-pointer', '-mmacosx-version-min=10.10')

platform.platform = Darwin_x86_64()

# -- rffi imports --

eci = ExternalCompilationInfo(libraries=["c++"], separate_module_files=["audio.cpp"], includes=["audio.h"], include_dirs=[os.getcwd()], frameworks=["OpenAL"], use_cpp_linker=True, platform=Darwin_x86_64())
#link_extra=["-stdlib=libc++", "-mmacosx-version-min=10.10"]

audio_init = rffi.llexternal("audio_init", [], lltype.Void, compilation_info=eci)
audio_deinit = rffi.llexternal("audio_deinit", [], lltype.Void, compilation_info=eci)
audio_feed_sample = rffi.llexternal("audio_feed_sample", [lltype.Float], lltype.Void, compilation_info=eci)
audio_sleep = rffi.llexternal("audio_sleep", [lltype.Float], lltype.Void, compilation_info=eci)
unpack_float = rffi.llexternal("unpack_float", [lltype.Char, lltype.Char, lltype.Char, lltype.Char], lltype.Float, compilation_info=eci)

# -- helpers --

from rpython.rlib import streamio
import array
import struct

def slicer(iterator):
    while True:
        yield (iterator.next(), iterator.next(), iterator.next(), iterator.next())

def read_samples(filename):
    stream = streamio.open_file_as_stream(filename)
    data = stream.readall()

    return [unpack_float(a, b, c, d) for a, b, c, d in slicer(iter(data))]

# -- class structure --

class Graph:
    def __init__(self):
        self.nodes = []
        self.connections = []

    def add_node(self, node):
        self.nodes.append(node)

    def connect(self, source_port, destination_port):
        destination_port.mapped_output_port = source_port

        self.connections.append(Connection(source_port, destination_port))

    def compile_render_program(self):
        result = []

        unsorted = {}
        for node in self.nodes:
            unsorted[node] = []

        for connection in self.connections:
            unsorted[connection.destination.owner()].append(connection.source.owner())

        while unsorted:
            acyclic = False

            for node, edges in unsorted.items():
                for edge in edges:
                    if edge in unsorted:
                        break
                else:
                    acyclic = True
                    del unsorted[node]
                    result.append(node)

            if not acyclic:
                raise RuntimeError("Graph has cycles!")

        print(result)
        return result

class Node:
    def __init__(self):
        pass

    def render(self):
        pass

class InputPort:
    def __init__(self, owner):
        self.owner = owner
        self.mapped_output_port = OutputPort(None)

class OutputPort:
    def __init__(self, owner):
        self.value = 0.0
        self.owner = owner

class Connection:
    def __init__(self, source, destination):
        self.source = source
        self.destination = destination

# -- some nodes --

class OutputDevice(Node):
    def __init__(self):
        self.input = InputPort(weakref.ref(self))

    def render(self):
        audio_feed_sample(self.input.mapped_output_port.value)

class SamplePlayer(Node):
    def __init__(self, filename):
        self.output = OutputPort(weakref.ref(self))

        self.samples = read_samples(filename)
        self.length = len(self.samples)
        self.position = 0

    def render(self):
        self.output.value = self.samples[self.position]
        self.position = (self.position + 1) % self.length

class Oscillator(Node):
    def __init__(self, frequency):
        self.output = OutputPort(weakref.ref(self))

        self.t = 0.0
        self.omega = 2.0 * math.pi * frequency / 44100.0

    def render(self):
        self.output.value = math.sin(self.omega * self.t)
        self.t = self.t + 1.0

class Mixer(Node):
    def __init__(self):
        self.first = InputPort(weakref.ref(self))
        self.second = InputPort(weakref.ref(self))
        self.mix = InputPort(weakref.ref(self)) # value between -1 and 1, -1 is 0 dB first and 1 is 0dB second

        self.output = OutputPort(weakref.ref(self))

    def render(self):
        mix = self.mix.mapped_output_port.value * 0.5 + 0.5
        self.output.value = self.first.mapped_output_port.value * (1.0 - mix) + self.second.mapped_output_port.value * mix

# Combine and attenuate two signals
class Combiner(Node):
    def __init__(self, first_level, second_level):
        self.first = InputPort(weakref.ref(self))
        self.second = InputPort(weakref.ref(self))
        self.output = OutputPort(weakref.ref(self))

        self.first_level = first_level
        self.second_level = second_level

    def render(self):
        self.output.value = self.first_level * self.first.mapped_output_port.value + self.second_level * self.second.mapped_output_port.value

# delay the signal by a given number of seconds
class Delay(Node):
    def __init__(self, delay):
        self.input = InputPort(weakref.ref(self))
        self.output = OutputPort(weakref.ref(self))

        self.delay = int(delay * 44100.0)
        self.buffer = [0.0] * self.delay
        self.position = 0

    def render(self):
        self.buffer[(self.position - 1) % self.delay] = self.input.mapped_output_port.value
        self.output.value = self.buffer[self.position]
        self.position = (self.position + 1) % self.delay

# 12 dB biquad lowpass filter
class LowPass(Node):
    def __init__(self, cutoff):
        self.input = InputPort(weakref.ref(self))
        self.output = OutputPort(weakref.ref(self))

        # compute coefficients
        omega = 2.0 * math.pi * cutoff * (1.0 / 44100.0)
        cos_omega = math.cos(omega)
        alpha = math.sin(omega) / (2.0 * 7.0)
        scale = 1.0 / (1.0 + alpha)

        self.a1 = -scale * 2.0 * cos_omega
        self.a2 = -scale * (alpha - 1.0)
        self.b1 = scale * (1.0 - cos_omega)
        self.b0 = self.b1 * 0.5
        self.b2 = self.b0

        # set initial state
        self.x1 = 0.0
        self.x2 = 0.0
        self.y1 = 0.0
        self.y2 = 0.0

    def render(self):
        # Direct form 1 evaluation
        y0 = self.b0 * self.input.mapped_output_port.value + self.b1 * self.x1 + self.b2 * self.x2 - self.a1 * self.y1 - self.a2 * self.y2

        self.x2 = self.x1
        self.x1 = self.input.mapped_output_port.value
        self.y2 = self.y1
        self.y1 = y0

        self.output.value = y0

# -- bootstrapping --

def entry_point(argv):
    print(argv)

    # build graph
    graph = Graph()

    output_device = OutputDevice()
    graph.add_node(output_device)

    oscillator = Oscillator(0.1)
    graph.add_node(oscillator)

    sample_player = SamplePlayer("music.f32")
    graph.add_node(sample_player)

    lowpass = LowPass(800.0)
    graph.add_node(lowpass)

    mixer = Mixer()
    graph.add_node(mixer)

    delay = Delay(0.3)
    graph.add_node(delay)

    delay_combiner = Combiner(1.0, 0.7)
    graph.add_node(delay_combiner)

    # make connections
    graph.connect(sample_player.output, lowpass.input)
    graph.connect(sample_player.output, mixer.first)
    graph.connect(lowpass.output, mixer.second)
    graph.connect(oscillator.output, mixer.mix)
    graph.connect(mixer.output, delay.input)
    graph.connect(delay.output, delay_combiner.second)
    graph.connect(mixer.output, delay_combiner.first)
    graph.connect(delay_combiner.output, output_device.input)

    # compile render program
    render_program = graph.compile_render_program()

    # start audio
    print(audio_init())

    # render
    seconds = 60

    for x in range(seconds * 44100):
        for node in render_program:
            node.render()

    audio_sleep(seconds)

    return 0

def target(*args):
    return entry_point, None

if __name__ == "__main__":
    import sys
    entry_point(sys.argv)
