# A stand-in for Claude in a terminal: history that scrolls on the wheel
# (mouse reporting on), and a spinner on its last row that never rests.
import os, re, sys, time, select
history = ["history line %d" % i for i in range(300)]
offset = 0
sys.stdout.write("\x1b[?1000h\x1b[?1006h")
def draw(tick):
    rows = os.get_terminal_size().lines
    shown = history[max(0, len(history) - offset - (rows - 1)):len(history) - offset]
    out = "\x1b[H\x1b[2J" + "\r\n".join(shown) + "\x1b[%d;1H" % rows + "* working " + "|/-\\"[tick % 4]
    sys.stdout.write(out); sys.stdout.flush()
tick = 0
draw(tick)
buffer = b""
while True:
    ready, _, _ = select.select([0], [], [], 0.03)
    if ready:
        data = os.read(0, 1024)
        if not data:
            break
        buffer += data
        for button in re.findall(rb"\x1b\[<(\d+);\d+;\d+[Mm]", buffer):
            rows = os.get_terminal_size().lines
            if button == b"64": offset = min(len(history) - (rows - 1), offset + 3)
            if button == b"65": offset = max(0, offset - 3)
        buffer = b""
    tick += 1
    draw(tick)
