import os, subprocess, getpass, struct
from tkinter import Tk
from tkinter.filedialog import askopenfilename
from pathlib import Path

def get_shortcut_path(path: str) -> str:
    target = ''

    with open(path, 'rb') as stream:
        content = stream.read()
        # skip first 20 bytes (HeaderSize and LinkCLSID)
        # read the LinkFlags structure (4 bytes)
        lflags = struct.unpack('I', content[0x14:0x18])[0]
        position = 0x18
        # if the HasLinkTargetIDList bit is set then skip the stored IDList 
        # structure and header
        if (lflags & 0x01) == 1:
            position = struct.unpack('H', content[0x4C:0x4E])[0] + 0x4E
        last_pos = position
        position += 0x04
        # get how long the file information is (LinkInfoSize)
        length = struct.unpack('I', content[last_pos:position])[0]
        # skip 12 bytes (LinkInfoHeaderSize, LinkInfoFlags, and VolumeIDOffset)
        position += 0x0C
        # go to the LocalBasePath position
        lbpos = struct.unpack('I', content[position:position+0x04])[0]
        position = last_pos + lbpos
        # read the string at the given position of the determined length
        size= (length + last_pos) - position - 0x02
        temp = struct.unpack('c' * size, content[position:position+size])
        target = ''.join([chr(ord(a)) for a in temp])
        
        return target

def get_exe_path(app: str) -> str:
    username = getpass.getuser()
    start_menu = f'C:\\Users\\{username}\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs'
    start_menu2 = f'C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs'
        
    for subdir, dirs, files in os.walk(start_menu):
        for file in files:
            if file.endswith('.lnk'):
                if app.lower() in file.lower():
                    return get_shortcut_path(f'{subdir}/{file}')
                    
    for subdir, dirs, files in os.walk(start_menu2):
        for file in files:
            if file.endswith('.lnk'):
                if app.lower() in file.lower():
                    return get_shortcut_path(f'{subdir}/{file}')



def open_app(app: str) -> None:
    try:
        app = get_exe_path(app)
        subprocess.run(app)
    except:
        Tk().withdraw()
        path = askopenfilename(title=f'Escolha o arquivo executável para {app}', initialdir=Path.home(), filetypes=[('Arquivo executável', '*.exe')])
        os.startfile(path)
        username = getpass.getuser()
        with open(f'doc/mem/{username}.txt', 'w+') as log:
            log.write(f'{app}=={path}')

def main():
    p = get_exe_path('Command Prompt')
    print(p)
    subprocess.Popen(p)

if __name__ == '__main__':
    main()