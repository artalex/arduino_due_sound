__author__ = 'artalex'

import collections
import optparse
import os
import struct
import sys

def readHeader(file):
    buffer = file.read(44)
    if buffer[0:4] != b'RIFF'\
        or buffer[8:12] != b'WAVE'\
        or buffer[12:16] != b'fmt '\
        or buffer[36:40] != b'data':
        return None

    header = collections.namedtuple('WavHeader', ['format', 'channels', 'sampleRate', 'byteRate', 'blockAlign', 'bitsPerSample', 'size'])
    header.format = struct.unpack('<H', buffer[20:22])[0]
    header.channels = struct.unpack('<H', buffer[22:24])[0]
    header.sampleRate = struct.unpack('<I', buffer[24:28])[0]
    header.byteRate = struct.unpack('<I', buffer[28:32])[0]
    header.blockAlign = struct.unpack('<H', buffer[32:34])[0]
    header.bitsPerSample = struct.unpack('<H', buffer[34:36])[0]
    header.size = struct.unpack('<I', buffer[40:44])[0]

    if header.channels * header.sampleRate * header.bitsPerSample / 8 != header.byteRate:
        return None

    return header

def perform():
    parser = optparse.OptionParser()
    parser.add_option("-i", "--input", dest="input", help="input file")
    (options, args) = parser.parse_args()

    if not options.input:
        print("Input file isn't specified")
        sys.exit(1)

    file = open(options.input, 'rb')

    header = readHeader(file)
    if not header:
        print("File hasn't wav format")
        sys.exit(1)

    print('Format: {0}\nChannels: {1}\nFrequency: {2}\nBits per sample: {3}'.format(header.format, header.channels, header.sampleRate, header.bitsPerSample))

    if header.format != 1 or (header.channels not in (1,2,)) or header.bitsPerSample != 16:
        print("File has not supported format")
        sys.exit(1)

    data = file.read(header.size)
    hex = []
    count = 0
    max = -100000
    min = 100000
    while count < len(data):
        sample = struct.unpack('<h', data[count : count + 2])[0]
        count += 2

        if header.channels == 2:
            sample += struct.unpack('<h', data[count : count + 2])[0]
            sample /= 2
            count += 2

        sample = int((sample + 32768) * 0xfff / 65535)
        hex.append(sample)

        max = max if max > sample else sample
        min = min if min < sample else sample

    print('\nMin sample: {0}\nMax sample: {1}'.format(min, max))

    outFileName = os.path.join(os.path.dirname(options.input), os.path.splitext(os.path.basename(options.input))[0])
    fileBin = open(outFileName + '.bin', 'wb')
    fileHex = open(outFileName + '.hex', 'wt')

    print('\n')
    count = 0
    for s in hex:
        if count and not count % 20:
            fileHex.write('\n')

        fileHex.write('0x{0:03x}, '.format(s))
        count += 1

        fileBin.write(struct.pack('<h', s))

    fileBin.close()
    fileHex.close()

if __name__== "__main__":
    perform()