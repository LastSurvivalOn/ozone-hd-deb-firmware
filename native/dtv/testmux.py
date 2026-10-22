#!/usr/bin/python3
"""testmux.py: a DVB test multiplex for ozone-tv --test, without an antenna.

FFmpeg makes the pictures and sound: service 101 "Test One" (H.264, audio in Ukrainian and in
English), 102 "Test Two" (H.264, one audio) and 103 "Test Radio" (audio only). This adds what
FFmpeg can't: on 101, DVB subtitles (EN 300 743, a line of text every 3 s) and teletext (EN 300
472/706: pages 100 and 101 with colored links, subtitles on page 888), and an EIT present/following
for 101 rated 16+, so the TV's subtitles, teletext, info panel and age lock can be tried. The
inserted packets take the place of null packets of FFmpeg's constant rate.

  ./testmux.py OUT.ts [SECONDS]      (default 30; needs ffmpeg with libx264, and PIL)
  then on the box: ozone-tv --test OUT.ts scan, and ozone-tv --test OUT.ts
"""
import datetime
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

SUB_PID, TTX_PID = 0x150, 0x151
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

# ---- PSI ----------------------------------------------------------------------------------------

CRC = []
for i in range(256):
    c = i << 24
    for _ in range(8):
        c = (c << 1) ^ 0x04c11db7 if c & 0x80000000 else c << 1
    CRC.append(c & 0xffffffff)


def crc32(b):
    c = 0xffffffff
    for x in b:
        c = ((c << 8) & 0xffffffff) ^ CRC[((c >> 24) ^ x) & 0xff]
    return c


def ts_packets(pid, payload, cc, pusi_pointer=False):
    """payload over TS packets of pid (a PES, or a section with its pointer field); cc a [counter]"""
    out = []
    first = True
    if pusi_pointer:
        payload = b'\0' + payload
    while payload:
        chunk, payload = payload[:184], payload[184:]
        head = bytes([0x47, (0x40 if first else 0) | pid >> 8, pid & 0xff])
        if len(chunk) < 184:   # stuffing in an adaptation field, so a PES ends where it should
            n = 184 - len(chunk)
            af = bytes([n - 1]) + (bytes([0]) + b'\xff' * (n - 2) if n > 1 else b'')
            out.append(head + bytes([0x30 | cc[0]]) + af + chunk)
        else:
            out.append(head + bytes([0x10 | cc[0]]) + chunk)
        cc[0] = (cc[0] + 1) & 15
        first = False
    return out


def pes(stream_id, data, pts, header_len=None):
    ptsb = bytes([0x21 | (pts >> 29) & 0x0e, (pts >> 22) & 0xff, 0x01 | (pts >> 14) & 0xfe,
                  (pts >> 7) & 0xff, 0x01 | (pts << 1) & 0xfe])
    hdr = ptsb + b'\xff' * ((header_len or 5) - 5)
    body = bytes([0x84, 0x80, len(hdr)]) + hdr + data
    return b'\0\0\1' + bytes([stream_id]) + struct.pack('>H', len(body)) + body


def bcd(n):
    return (n // 10) << 4 | n % 10


def mjd_bcd(t):
    d = datetime.datetime.fromtimestamp(t, datetime.timezone.utc)
    mjd = (d.date() - datetime.date(1858, 11, 17)).days
    return struct.pack('>H', mjd) + bytes([bcd(d.hour), bcd(d.minute), bcd(d.second)])


def eit(sid, tsid, onid, now):
    """Present/following of sid: a programme on now, rated 16+, and the next one"""
    def event(eid, start, dur, title, text, age):
        name = b'\x01' + title.encode('iso8859-5')   # ISO 8859-5, as Zeonbud sends
        txt = b'\x01' + text.encode('iso8859-5')
        short = b'ukr' + bytes([len(name)]) + name + bytes([len(txt)]) + txt
        desc = bytes([0x4d, len(short)]) + short
        if age:
            desc += bytes([0x55, 4]) + b'UKR' + bytes([age - 3])
        return struct.pack('>H', eid) + mjd_bcd(start) + bytes([bcd(dur // 3600), bcd(dur // 60 % 60), 0]) + \
            struct.pack('>H', 0x8000 | len(desc)) + desc
    body = struct.pack('>HHBB', tsid, onid, 1, 0x4e)
    body += event(1, now - 600, 120 * 60, 'Тестова передача', 'Субтитри, телетекст і вікове обмеження 16+.', 16)
    body += event(2, now + 6600, 60 * 60, 'Наступна передача', 'Без обмежень.', 0)
    sec = bytes([0x4e]) + struct.pack('>HHBBB', 0xf000 | (5 + len(body) + 4), sid, 0xc1, 0, 1) + body
    return sec + struct.pack('>I', crc32(sec))


# ---- DVB subtitles (EN 300 743) -------------------------------------------------------------

class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = self.acc << 1 | (value >> i) & 1
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def align(self):
        while self.n:
            self.put(0, 1)


def line_4bit(px):
    """One line as a 4-bit/pixel code string"""
    b = Bits()
    i = 0
    while i < len(px):
        c, n = px[i], 1
        while i + n < len(px) and px[i + n] == c and n < 280:
            n += 1
        if c and n < 4:
            for _ in range(n):
                b.put(c, 4)
        elif n >= 25:
            b.put(0, 4), b.put(0b1111, 4), b.put(n - 25, 8), b.put(c, 4)
        elif n >= 9:
            b.put(0, 4), b.put(0b1110, 4), b.put(n - 9, 4), b.put(c, 4)
        elif n >= 4:
            n = min(n, 7)
            b.put(0, 4), b.put(0b10, 2), b.put(n - 4, 2), b.put(c, 4)
        elif n == 3:   # colour 0
            b.put(0, 4), b.put(0, 1), b.put(1, 3)
        else:          # one or two pixels of colour 0
            b.put(0, 4), b.put(0b11, 2), b.put(n - 1, 2)
        i += n
    b.put(0, 4), b.put(0, 1), b.put(0, 3)   # end of string
    b.align()
    return bytes([0x11]) + bytes(b.out) + b'\xf0'


def subtitle_pes(text, version, pts):
    W, H = 620, 56
    img = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT, 34)
    tw = d.textlength(text, font=font)
    d.text(((W - tw) / 2, 8), text, font=font, fill=255, stroke_width=3, stroke_fill=128)
    # 0 transparent, 1 white, 2 black (the outline)
    px = [[1 if v > 200 else 2 if v > 60 else 0 for v in (img.getpixel((x, y)) for x in range(W))] for y in range(H)]
    top = b''.join(line_4bit(px[y]) for y in range(0, H, 2))
    bottom = b''.join(line_4bit(px[y]) for y in range(1, H, 2))

    def seg(kind, data):
        return bytes([0x0f, kind]) + struct.pack('>HH', 1, len(data)) + data
    v = version & 15
    page = bytes([3, v << 4 | 2 << 2 | 3]) + bytes([1, 0xff]) + struct.pack('>HH', 50, 470)
    region = bytes([1, v << 4 | 0x08 | 7]) + struct.pack('>HH', W, H) + bytes([2 << 5 | 2 << 2 | 3, 1, 0, 0]) + \
        struct.pack('>HH', 1, 0x0000) + struct.pack('>H', 0)
    clut = bytes([1, v << 4 | 0xf])
    for entry, (y, cr, cb, t) in enumerate(((16, 128, 128, 255), (235, 128, 128, 0), (16, 128, 128, 0))):
        clut += bytes([entry, 0x40 | 0x01, y, cr, cb, t])   # 4-bit CLUT, full range
    obj = struct.pack('>HB', 1, v << 4 | 0x00) + struct.pack('>HH', len(top), len(bottom)) + top + bottom
    data = b'\x20\x00' + seg(0x10, page) + seg(0x11, region) + seg(0x12, clut) + seg(0x13, obj) + \
        seg(0x80, b'') + b'\xff'
    return pes(0xbd, data, pts)


# ---- teletext (EN 300 706, in PES as EN 300 472 has it) ----------------------------------------

HAM = [0x15, 0x02, 0x49, 0x5e, 0x64, 0x73, 0x38, 0x2f, 0xd0, 0xc7, 0x8c, 0x9b, 0xa1, 0xb6, 0xfd, 0xea]


def rev(b):
    return int('{:08b}'.format(b)[::-1], 2)


def parity(c):
    return c | (0 if bin(c).count('1') % 2 else 0x80)


def text40(s, n=40):
    return bytes(parity(ord(c) if ord(c) < 128 else ord('?')) for c in s.ljust(n)[:n])


def mrag(mag, row):
    return bytes([HAM[(mag & 7) | (row & 1) << 3], HAM[row >> 1]])


def header(mag, page, text, subtitle=False):
    """Packet X/0: the page number, C4 (erase), C5/C6 (newsflash, subtitle), the header text"""
    s4 = 0xc if subtitle else 0
    return mrag(mag, 0) + bytes([HAM[page & 15], HAM[page >> 4], HAM[0], HAM[0x8], HAM[0], HAM[s4], HAM[0],
                                 HAM[0]]) + text40(text, 32)


def row(mag, n, s):
    return mrag(mag, n) + text40(s)


def flof(mag, links):
    """Packet X/27/0: the red, green, yellow, blue links (and two unused)"""
    out = mrag(mag, 27) + bytes([HAM[0]])
    for page in links + [0xff, 0xff]:
        m = ((page >> 8) & 7) ^ (mag & 7)
        out += bytes([HAM[page & 15], HAM[(page >> 4) & 15], HAM[0xf], HAM[0x7 | (m & 1) << 3], HAM[0xf],
                      HAM[0x3 | (m >> 1) << 2]])
    return out + bytes([HAM[0x8]]) + b'\0\0'


def ttx_pes(packets, pts, subtitle=False):
    """Up to three teletext packets in one PES, one TS packet"""
    data = b'\x10'
    for i, p in enumerate(packets):
        data += bytes([0x03 if subtitle else 0x02, 0x2c, 0xc0 | 0x20 | (7 + i), 0xe4]) + bytes(rev(b) for b in p)
    while len(data) < 1 + 3 * 46:
        data += b'\xff\x2c' + b'\xff' * 44   # stuffing data units
    return pes(0xbd, data, pts, header_len=36)


def ttx_cycle(n, clock):
    """The pages, once round: lists of up to three packets"""
    p100 = [header(1, 0x00, 'OZONE TT 100 ' + clock),
            row(1, 2, '  Ozone teletext test page 100'),
            row(1, 4, '  This is what a page looks like:'),
            row(1, 5, '  40 columns, 25 rows, 8 colours.'),
            row(1, 8, '\x01  Red\x02  Green\x03  Yellow\x04  Blue'),
            row(1, 12, '  Red: page 101     Green: page 888'),
            flof(1, [0x101, 0x888, 0x100, 0x100])]
    p101 = [header(1, 0x01, 'OZONE TT 101 ' + clock),
            row(1, 2, '  Page 101: the second page'),
            row(1, 4, '  Red: back to 100'),
            flof(1, [0x100, 0x888, 0x100, 0x100])]
    p888 = [header(0, 0x88, 'OZONE SUBS 888 ' + clock, subtitle=True),
            row(0, 20, '\x0b\x0b Teletext subtitle %d \x0a\x0a' % n)]
    out = []
    for page in (p100, p101):
        out += [page[i:i + 3] for i in range(0, len(page), 3)]
    out.append((p888, True))
    return out


# ---- the multiplex ----------------------------------------------------------------------------

def make_base(path, seconds):
    t = str(seconds)
    cmd = ['ffmpeg', '-v', 'error', '-y',
           '-f', 'lavfi', '-i', 'testsrc2=size=720x576:rate=25,drawtext=fontfile=%s:text=ONE:fontsize=90:x=40:y=40' % FONT,
           '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
           '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
           '-f', 'lavfi', '-i', 'testsrc=size=720x576:rate=25,drawtext=fontfile=%s:text=TWO:fontsize=90:x=40:y=40' % FONT,
           '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000',
           '-f', 'lavfi', '-i', 'sine=frequency=330:sample_rate=48000,volume=0.5',
           '-map', '0:v', '-map', '1:a', '-map', '2:a', '-map', '3:v', '-map', '4:a', '-map', '5:a',
           '-t', t, '-c:v', 'libx264', '-preset', 'veryfast', '-profile:v', 'main', '-b:v', '1500k',
           '-maxrate', '1500k', '-bufsize', '1500k', '-g', '25', '-pix_fmt', 'yuv420p',
           '-c:a', 'mp2', '-b:a', '192k',
           '-metadata:s:a:0', 'language=ukr', '-metadata:s:a:1', 'language=eng',
           '-metadata:s:a:2', 'language=ukr', '-metadata:s:a:3', 'language=ukr',
           '-program', 'program_num=101:title=Test One:st=0:st=1:st=2',
           '-program', 'program_num=102:title=Test Two:st=3:st=4',
           '-program', 'program_num=103:title=Test Radio:st=5',
           '-f', 'mpegts', '-muxrate', '7000000', '-mpegts_service_type', 'digital_tv',
           '-mpegts_flags', 'system_b', path]
    subprocess.run(cmd, check=True)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_path = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    with tempfile.NamedTemporaryFile(suffix='.ts') as tmp:
        make_base(tmp.name, seconds)
        data = open(tmp.name, 'rb').read()

    pkts = [data[i:i + 188] for i in range(0, len(data) - 187, 188)]
    # the PAT, then the PMT of 101: its PID and PCR PID
    pmt_pid = pcr_pid = None
    tsid = 1
    for p in pkts:
        pid = (p[1] & 0x1f) << 8 | p[2]
        if pid == 0 and p[1] & 0x40:
            s = p[5:]
            tsid = s[3] << 8 | s[4]
            n = ((s[1] & 15) << 8 | s[2]) + 3 - 4
            for i in range(8, n, 4):
                if (s[i] << 8 | s[i + 1]) == 101:
                    pmt_pid = (s[i + 2] & 0x1f) << 8 | s[i + 3]
        if pmt_pid and pid == pmt_pid and p[1] & 0x40:
            pcr_pid = (p[13] & 0x1f) << 8 | p[14]
            break
    print('service 101: PMT 0x%x, PCR 0x%x' % (pmt_pid, pcr_pid))

    def new_pmt(p):
        s = p[5:]
        length = (s[1] & 15) << 8 | s[2]
        sec = bytearray(s[:3 + length - 4])
        subs = b'\x59\x08' + b'ukr' + bytes([0x10]) + struct.pack('>HH', 1, 1)
        ttx = b'\x56\x0a' + b'ukr' + bytes([0x01 << 3 | 1, 0x00]) + b'ukr' + bytes([0x02 << 3 | 0, 0x88])
        sec += struct.pack('>BHH', 0x06, 0xe000 | SUB_PID, 0xf000 | len(subs)) + subs
        sec += struct.pack('>BHH', 0x06, 0xe000 | TTX_PID, 0xf000 | len(ttx)) + ttx
        n = len(sec) + 4 - 3
        sec[1], sec[2] = 0xb0 | n >> 8, n & 0xff
        sec = bytes(sec) + struct.pack('>I', crc32(sec))
        return bytes(p[:4]) + (b'\0' + sec).ljust(184, b'\xff')

    out = []
    queue = []
    cc_sub, cc_ttx, cc_eit = [0], [0], [0]
    clock = 0            # 90 kHz, from 101's PCR
    next_ttx = next_sub = next_eit = 0
    n_sub = n_ttx = 0
    now = int(datetime.datetime.now().timestamp())
    cycle = []
    inserted = 0
    for p in pkts:
        pid = (p[1] & 0x1f) << 8 | p[2]
        if pid == pcr_pid and p[3] & 0x20 and p[4] >= 7 and p[5] & 0x10:
            clock = (p[6] << 25 | p[7] << 17 | p[8] << 9 | p[9] << 1 | p[10] >> 7)
        if pid == pmt_pid and p[1] & 0x40:
            p = new_pmt(p)
        if clock and clock >= next_ttx:
            next_ttx = clock + 90000 * 4 // 100   # a PES every 40 ms
            if not cycle:
                n_ttx += 1
                stamp = '%02d:%02d:%02d' % (clock // 90000 // 3600 % 24, clock // 90000 // 60 % 60, clock // 90000 % 60)
                cycle = ttx_cycle(n_ttx, stamp)
            item = cycle.pop(0)
            sub = isinstance(item, tuple)
            pk = item[0] if sub else item
            queue += ts_packets(TTX_PID, ttx_pes(pk, clock + 9000, sub), cc_ttx)
        if clock and clock >= next_sub:
            next_sub = clock + 3 * 90000
            n_sub += 1
            queue += ts_packets(SUB_PID, subtitle_pes('Субтитри DVB, рядок %d' % n_sub, n_sub, clock + 45000), cc_sub)
        if clock and clock >= next_eit:
            next_eit = clock + 2 * 90000
            queue += ts_packets(0x12, eit(101, tsid, 1, now), cc_eit, pusi_pointer=True)
        if pid == 0x1fff and queue:
            p = queue.pop(0)
            inserted += 1
        out.append(p)
    with open(out_path, 'wb') as f:
        f.write(b''.join(out))
    print('%s: %.1f MB, %d packets inserted, %d left over' % (out_path, len(out) * 188 / 1e6, inserted, len(queue)))


if __name__ == '__main__':
    main()
