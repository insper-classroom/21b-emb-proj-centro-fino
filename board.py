import pyautogui, serial, argparse, time, logging, numpy as np, os, subprocess, getpass, struct, platform
#import alsaaudio
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
        if not(subdir.startswith('Windows')):
            for file in files:
                if file.endswith('.lnk'):
                    if app in file:
                        return get_shortcut_path(f'{subdir}/{file}')
                    
    for subdir, dirs, files in os.walk(start_menu2):
        if not(subdir.startswith('Windows')):
            for file in files:
                if file.endswith('.lnk'):
                    if app in file:
                        return get_shortcut_path(f'{subdir}/{file}')

def open_app(app: str) -> None:
    try:
        path = get_exe_path(app)
        subprocess.Popen(path)
    except:
        Tk().withdraw()
        path = askopenfilename(title=f'Escolha o arquivo executável para {app}', initialdir=Path.home(), filetypes=[('Arquivo executável', '*.exe')])
        subprocess.Popen(path)
        username = getpass.getuser()
        with open(f'doc/mem/{username}.txt', 'w+') as log:
            log.write(f'{app}=={path}')

class MyControllerMap:
    def __init__(self):
        self.button = {'T': '', 'G': '', 'V': '', 'C': ''}
 
 
class SerialControllerInterface:
    # Protocolo
    # byte 1 -> Botão 1 (estado - Apertado 1 ou não 0)
    # byte 2 -> EOP - End of Packet -> valor reservado 'X'
 
    def __init__(self, port, baudrate):
        self.ser = serial.Serial(port, baudrate=baudrate)
        self.mapping = MyControllerMap()
        self.incoming = '0'
        self.last_button = ''
        pyautogui.PAUSE = 0  ## remove delay
        
 
    def update(self):
        # Receber qual tecla foi clicada
        #data = self.ser.read()
        encoding = 'utf-8'
        EOP = 'X'
        valor_lido = ''
        list_received_data = []
        is_eop = False

        while not is_eop:
            valor_lido = self.ser.read(size=1)
            is_eop = (valor_lido == b'X')

            if not is_eop:
                list_received_data.append(valor_lido)
                # print(f'Valor lido cru: {valor_lido}')
                # # print(f'Valor lido encoding: {valor_lido.decode(encoding)}')
                # print(f'Valor lido decode bytes: {int.from_bytes(valor_lido, byteorder="little")}')
                # print(f'Valor lido é x? {valor_lido == "X"}')
                # print('\n')

        print(list_received_data)
        if list_received_data[0] == b'P':
            valor_byte = list_received_data[1]
            valor = int.from_bytes(valor_byte, byteorder="little")
            print(f'Valor potenciometro: {valor}')
        print('Next package \n')


        # print(data[0], data[1])
        # if not (data[0] == b'X'):
        #     flag = int.from_bytes(data, byteorder='little')
        #     data = self.ser.read()
        #     button = int.from_bytes(data, byteorder='little')
        #     print((button))
        #     logging.debug("Received DATA: {}".format(data))
            
        #     if flag == 1:         
        #         if button == 1:
        #             print('Teams')
        #             open_app('Teams')
                    
        #         elif button == 2:
        #             print('Chrome')
        #             open_app('Chrome')
                    
        #         elif button == 3:
        #             print('VSCode')
        #             open_app('Code')
                    
        #         elif button == 4:
        #             print('cmd')
        #             open_app('Command Prompt')
    
        # self.incoming = self.ser.read()
 
 
class DummyControllerInterface:
    def __init__(self):
        self.mapping = MyControllerMap()
 
    def update(self):
        pyautogui.keyDown(self.mapping.button['A'])
        time.sleep(0.1)
        pyautogui.keyUp(self.mapping.button['A'])
        logging.info("[Dummy] Pressed A button")
        time.sleep(1)
 
 
if __name__ == '__main__':
    interfaces = ['dummy', 'serial']
    argparse = argparse.ArgumentParser()
    argparse.add_argument('serial_port', type=str)
    argparse.add_argument('-b', '--baudrate', type=int, default=9600)
    argparse.add_argument('-c', '--controller_interface',
                          type=str, default='serial', choices=interfaces)
    argparse.add_argument('-d', '--debug', default=False, action='store_true')
    args = argparse.parse_args()
    if args.debug:
        logging.basicConfig(level=logging.DEBUG)
 
    print("Connection to {} using {} interface ({})".format(
        args.serial_port, args.controller_interface, args.baudrate))
    if args.controller_interface == 'dummy':
        controller = DummyControllerInterface()
    else:
        controller = SerialControllerInterface(
            port=args.serial_port, baudrate=args.baudrate)
 
    while True:
        try:
            controller.update()
        except:
            os._exit()