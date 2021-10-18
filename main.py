import os, subprocess, getpass
from tkinter import Tk
from tkinter.filedialog import askopenfilename
from pathlib import Path

def open_app(app: str) -> None:
    try:
        subprocess.run(app)
    except:
        Tk().withdraw()
        path = askopenfilename(title=f'Escolha o arquivo executável para {app}', initialdir='C:/', filetypes=[('Arquivo executável', '*.exe')])
        os.startfile(path)
        username = getpass.getuser()
        with open(f'doc/mem/{username}.txt', 'w+') as log:
            log.write(f'{app}=={path}')

def main():
    open_app('Discord')

if __name__ == '__main__':
    main()