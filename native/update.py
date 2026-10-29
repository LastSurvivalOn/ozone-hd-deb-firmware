#!/usr/bin/python3
"""update.py: brings the box up to this tree of the scripts, on the box itself, as root.

ozone-system (Updates) runs it from a tree it downloaded from GitHub; install-desktop.sh runs it
with --dotfiles-only for the user's files. Only the Python standard library.

  update.py --native DIR --user NAME [--commit SHA --date DD.MM.YYYY] [--dotfiles-only]

What it does:
- system files (DIR/rootfs): each one the box has and that differs is replaced. A file the box
  hasn't got is installed when the program it belongs to is: the install script that names it
  (install-desktop.sh's are always, the others' when some file only they name is on the box);
  files no install script names are prepare-debian.sh's, always installed;
- the user's files (DIR/dotfiles): replaced only when the user hasn't changed them since they were
  installed (their sha256 in /var/lib/ozone/dotfiles.sha256, or among --known's, the versions in
  git history that install-desktop.sh sends); changed ones are kept. A copy of ours goes to
  /usr/local/share/ozone/dotfiles, what the factory reset puts back;
- the Debian packages the installed parts' install scripts install (their apt-get install lines);
- amplay, rebuilt on the box when its sources changed;
- what only a computer can build (kernel modules, the TV frontend, WebKit...): said, when its
  sources changed since the last update.

Its output: "@@ step" lines, "!! something to do" lines, and last "== reboot", "== desktop" (restart
the desktop) or "== nothing".
"""
import argparse
import hashlib
import json
import os
import pwd
import re
import shutil
import subprocess
import sys
import tempfile

STATE = '/var/lib/ozone'
DOTFILES_SUMS = STATE + '/dotfiles.sha256'
SOURCES_SUMS = STATE + '/sources.json'
AMPLAY_SUM = STATE + '/amplay.sha256'
PRISTINE = '/usr/local/share/ozone/dotfiles'
# never replaced: made for this box at install time (fstab, the network), or written by our tools
SKIP = {'etc/fstab', 'etc/network/interfaces', 'etc/asound.conf'}
AMPLAY = ['amplay.c', 'osd.c', 'osd.h', 'ttx.c', 'ttx.h', 'Makefile']
# built on a computer, with what to run there when their sources change
COMPUTER = {'gpu/kernel': ('the GPU kernel modules', 'install-gpu.sh'),
            'gpu/ddx': ('the GPU X driver', 'install-gpu.sh'),
            'gpu/egl-fix': ('the GPU EGL library', 'install-gpu.sh'),
            'bluetooth': ('the Bluetooth driver', 'install-bluetooth.sh'),
            'dtv': ('the TV tuner program', 'install-tv.sh'),
            'vpn': ('wireguard-go and xfrm_user.ko', 'install-network.sh'),
            'miracast': ('the Miracast wpa_supplicant', 'install-miracast.sh'),
            'webkit': ("the browser's WebKit", 'install-webkit.sh')}
# which install scripts' parts are on the box: a file each of them puts there
MARKERS = {'install-desktop.sh': None, 'prepare-debian.sh': None,
           'install-gpu.sh': '/usr/local/bin/ozone-gl', 'install-games.sh': '/usr/local/bin/retroarch',
           'install-network.sh': '/usr/local/bin/ozone-network',
           'install-miracast.sh': '/usr/local/bin/ozone-miracast',
           'install-bluetooth.sh': '/etc/init.d/ozone-bluetooth', 'install-tv.sh': '/usr/local/bin/ozone-tv',
           'install-android-files.sh': '/etc/init.d/ozone-android', 'install-webkit.sh': '/usr/local/lib/ozone-webkit'}


def say(kind, text):
    print('%s %s' % (kind, text), flush=True)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def tree_sum(paths):
    h = hashlib.sha256()
    for p in paths:
        if os.path.isfile(p):
            h.update(os.path.basename(p).encode() + b'\0')
            with open(p, 'rb') as f:
                h.update(f.read())
    return h.hexdigest()


def dir_sum(top):
    files = []
    for root, dirs, names in os.walk(top):
        dirs.sort()
        files += [os.path.join(root, n) for n in sorted(names)]
    h = hashlib.sha256()
    for p in files:
        h.update(os.path.relpath(p, top).encode() + b'\0')
        with open(p, 'rb') as f:
            h.update(f.read())
    return h.hexdigest()


def files_under(top):
    out = []
    for root, dirs, names in os.walk(top):
        out += [os.path.relpath(os.path.join(root, n), top) for n in names]
    return sorted(out)


def install_file(src, dst, uid=0, gid=0):
    """A copy that replaces dst at once, with src's mode"""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + '.ozone-new'
    shutil.copyfile(src, tmp)
    os.chmod(tmp, os.stat(src).st_mode & 0o7777)
    os.chown(tmp, uid, gid)
    os.replace(tmp, dst)


def load_sums(path):
    out = {}
    try:
        with open(path) as f:
            for line in f:
                parts = line.rstrip('\n').split('  ', 1)
                if len(parts) == 2:
                    out.setdefault(parts[1], set()).add(parts[0])
    except OSError:
        pass
    return out


# ---- which parts are installed --------------------------------------------------------------------

def scripts(native):
    out = {}
    for name in sorted(os.listdir(native)):
        if re.match(r'(install-.*|prepare-debian)\.sh$', name):
            with open(os.path.join(native, name)) as f:
                out[name] = f.read()
    return out


def owners(rel, texts):
    """The install scripts that name this rootfs file or one of its folders"""
    parts = rel.split('/')
    # the file, or a folder of it four levels down or more (usr/local/share/ozone/geany)
    candidates = [rel] + ['/'.join(parts[:i]) for i in range(len(parts) - 1, 3, -1)]
    out = []
    for name, text in texts.items():
        if name == 'prepare-debian.sh':
            continue
        if any(re.search(r'(^|[\s"\'])' + re.escape(c) + r'($|[\s"\'\\|])', text, re.M) for c in candidates):
            out.append(name)
    return out


def installed(name):
    marker = MARKERS.get(name)
    return marker is None or os.path.exists(marker)


# ---- the steps ------------------------------------------------------------------------------------

def system_files(native, texts):
    root = os.path.join(native, 'rootfs')
    changed = []
    for rel in files_under(root):
        if rel in SKIP or '__pycache__' in rel:
            continue
        src, dst = os.path.join(root, rel), '/' + rel
        if os.path.lexists(dst):
            if os.path.islink(dst) or not os.path.isfile(dst):
                continue
            if sha256(src) == sha256(dst) and (os.stat(src).st_mode & 0o111) == (os.stat(dst).st_mode & 0o111):
                continue
        else:
            who = owners(rel, texts)
            if who and not any(installed(w) for w in who):
                continue
        install_file(src, dst)
        changed.append(rel)
        say('  ', rel)
    return changed


def dotfiles(native, user, known_path=None):
    """The user's files: ours replace theirs only if they are still ours"""
    pw = pwd.getpwnam(user)
    home = pw.pw_dir
    top = os.path.join(native, 'dotfiles')
    sums = load_sums(DOTFILES_SUMS)
    known = load_sums(known_path) if known_path else {}
    changed, kept, record = [], [], {}
    for rel in files_under(top):
        src, dst = os.path.join(top, rel), os.path.join(home, rel)
        new = sha256(src)
        ours = sums.get(rel, set()) | known.get(rel, set())
        if os.path.isfile(dst) and not os.path.islink(dst):
            cur = sha256(dst)
            same_mode = (os.stat(src).st_mode & 0o111) == (os.stat(dst).st_mode & 0o111)
            if cur == new and same_mode:
                record[rel] = new
                continue
            if cur != new and cur not in ours:
                kept.append(rel)
                say('  ', '%s: kept yours (you changed it)' % rel)
                record[rel] = next(iter(sums[rel])) if rel in sums else new
                continue
        elif os.path.lexists(dst):
            kept.append(rel)
            continue
        # its folders belong to the user, as tar made them
        d = os.path.dirname(dst)
        missing = []
        while not os.path.isdir(d) and d.startswith(home):
            missing.append(d)
            d = os.path.dirname(d)
        for d in reversed(missing):
            os.mkdir(d)
            os.chown(d, pw.pw_uid, pw.pw_gid)
        install_file(src, dst, pw.pw_uid, pw.pw_gid)
        record[rel] = new
        changed.append(rel)
        say('  ', rel)
    os.makedirs(STATE, exist_ok=True)
    with open(DOTFILES_SUMS + '.tmp', 'w') as f:
        for rel in sorted(record):
            f.write('%s  %s\n' % (record[rel], rel))
    os.replace(DOTFILES_SUMS + '.tmp', DOTFILES_SUMS)
    # the factory reset's copy
    tmp = PRISTINE + '.new'
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(top, tmp, symlinks=True)
    shutil.rmtree(PRISTINE, ignore_errors=True)
    os.rename(tmp, PRISTINE)
    return changed, kept


def apt_lines(text):
    """The packages of a script's apt-get install commands"""
    text = text.replace('\\\n', ' ')
    pkgs = []
    for m in re.finditer(r'apt-get install([^\n;&|>]*)', text):
        for tok in m.group(1).split():
            if tok.startswith('-') or tok.startswith('$'):
                continue
            if re.match(r'^[a-z0-9][a-z0-9+.-]+$', tok):
                pkgs.append(tok)
    return pkgs


def packages(texts):
    wanted = []
    for name, text in texts.items():
        if name != 'prepare-debian.sh' and installed(name):
            wanted += [p for p in apt_lines(text) if p not in wanted]
    missing = []
    for p in wanted:
        r = subprocess.run(['dpkg-query', '-W', '-f', '${Status}', p], capture_output=True, text=True)
        if 'install ok installed' not in r.stdout:
            missing.append(p)
    if not missing:
        return []
    say('@@', 'new Debian packages: ' + ' '.join(missing))
    env = dict(os.environ, DEBIAN_FRONTEND='noninteractive')
    r = subprocess.run(['apt-get', 'install', '-y', '-q', '--no-install-recommends'] + missing, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in r.stdout.splitlines()[-5:]:
        say('  ', line)
    if r.returncode != 0:
        say('!!', 'apt-get failed to install: ' + ' '.join(missing))
    check_init()
    return missing


def check_init():
    r = subprocess.run(['dpkg', '-S', '/sbin/init'], capture_output=True, text=True)
    if 'sysvinit-core' not in r.stdout:
        say('!!', '/sbin/init is not sysvinit any more: do not restart the box; reinstall sysvinit-core')
        sys.exit(1)


def amplay(native):
    src = os.path.join(native, 'amplay')
    if not os.path.isdir(src) or not os.path.exists('/usr/local/bin/amplay'):
        return False
    s = tree_sum([os.path.join(src, n) for n in AMPLAY])
    try:
        with open(AMPLAY_SUM) as f:
            if f.read().strip() == s:
                return False
    except OSError:
        pass
    say('@@', 'amplay (the video player), built on the box')
    d = tempfile.mkdtemp(prefix='amplay-')
    try:
        for n in AMPLAY:
            if os.path.exists(os.path.join(src, n)):
                shutil.copy(os.path.join(src, n), d)
        for step in (['make', '-s', '-C', d], ['make', '-s', '-C', d, 'install']):
            r = subprocess.run(step, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if r.returncode != 0:
                for line in r.stdout.splitlines()[-8:]:
                    say('  ', line)
                say('!!', 'amplay did not build: the old one stays')
                return False
    finally:
        shutil.rmtree(d, ignore_errors=True)
    with open(AMPLAY_SUM, 'w') as f:
        f.write(s + '\n')
    return True


def computer_parts(native):
    try:
        with open(SOURCES_SUMS) as f:
            old = json.load(f)
    except (OSError, ValueError):
        old = None
    new = {}
    for rel in COMPUTER:
        p = os.path.join(native, rel)
        if os.path.isdir(p):
            new[rel] = dir_sum(p)
    if old is not None:
        seen = set()
        for rel, (what, script) in COMPUTER.items():
            if rel in new and old.get(rel) != new[rel] and installed(script) and (what, script) not in seen:
                seen.add((what, script))
                say('!!', '%s changed: it is built on a computer (native/%s there)' % (what, script))
    with open(SOURCES_SUMS, 'w') as f:
        json.dump(new, f, indent=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--native', required=True)
    ap.add_argument('--user', required=True)
    ap.add_argument('--commit')
    ap.add_argument('--date')
    ap.add_argument('--known')
    ap.add_argument('--dotfiles-only', action='store_true')
    ap.add_argument('--amplay-built', action='store_true', help='amplay was just built from DIR/amplay')
    a = ap.parse_args()
    if os.geteuid() != 0:
        sys.exit('update.py runs as root')
    native = os.path.abspath(a.native)
    os.makedirs(STATE, exist_ok=True)
    os.umask(0o022)
    if a.amplay_built:
        with open(AMPLAY_SUM, 'w') as f:
            f.write(tree_sum([os.path.join(native, 'amplay', n) for n in AMPLAY]) + '\n')
    if a.dotfiles_only:
        say('@@', 'your files')
        dotfiles(native, a.user, a.known)
        return
    texts = scripts(native)
    say('@@', 'system files')
    sys_changed = system_files(native, texts)
    say('@@', 'your files')
    dot_changed, _kept = dotfiles(native, a.user, a.known)
    new_pkgs = packages(texts)
    built = amplay(native)
    computer_parts(native)
    for rel in sys_changed:
        if rel.startswith('etc/init.d/'):
            name = os.path.basename(rel)
            subprocess.call(['update-rc.d', name, 'defaults'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if 'etc/init.d/ozone-media' in sys_changed or 'usr/local/bin/ozone-media' in sys_changed:
        # a daemon still running keeps its old code
        if subprocess.call(['/etc/ozone/is-native']) == 0:
            subprocess.call(['/etc/init.d/ozone-media', 'restart'], stdout=subprocess.DEVNULL)
    if a.commit:
        with open('/etc/ozone/commit', 'w') as f:
            f.write(a.commit + '\n')
        with open('/etc/ozone/version', 'w') as f:
            f.write('%s, %s\n' % (a.commit[:7], a.date or ''))
    subprocess.call(['sync'])
    session = ('usr/local/bin/', 'usr/local/share/')
    if any(not r.startswith(session) for r in sys_changed) or new_pkgs:
        say('==', 'reboot')
    elif sys_changed or dot_changed or built:
        say('==', 'desktop')
    else:
        say('==', 'nothing')


if __name__ == '__main__':
    main()
