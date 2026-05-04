#! /usr/bin/python3
import gpiod
import time
import os
import keyboard
import re
import multiprocessing
import click

from gpiod.line import Direction, Value, Edge
from gpiod.edge_event import EdgeEvent
from gpiod import LineRequest

GPIO_CLASS_PATH = "/dev/"

gpio_chip_device_name = None
gpio_chip = None

w_proc = None
is_waiting = False

def GpioTestList():
 
    os.system('clear')
    ListGpioChips()
    print('>------------------------------------<')
    print('1 : GPIO Chip Open')
    print('2 : GPIO Chip Close')
    print('3 : Get Chip Line List')
    print('4 : Set Output Value')
    print('5 : Get Input Value')
    print('6 : Wait Input Event')
    print()
    print('? : Command list')
    print('Q : Quit')
    print('>------------------------------------<')

def ListGpioChips():
    
    print("=======================================================")
    print("Available GPIO chips in the system:")
    print("Name\t\tLabel\t\t\t\tLines")

    # Loop through all files in the /dev/ directory and look for gpiochip*
    for entry in os.listdir(GPIO_CLASS_PATH):
        if entry.startswith("gpiochip"):
            chip_path = os.path.join(GPIO_CLASS_PATH, entry)
            
            try:
                chip = gpiod.Chip(chip_path)
                label = chip.get_info().label or "[No Label]"
                num_lines = chip.get_info().num_lines

                print(f"{entry}\t{label:<30}\t{num_lines}")
            except Exception as e:
                print(f"{entry}\t\t[Failed to open] ({e})")

    print("=======================================================")

def GpioChipOpen():

    global gpio_chip_device_name
    global gpio_chip

    print('GPIO device number(e.g if /dev/gpiochip1, input 1)?')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        gpio_chip_device_name = f"/dev/gpiochip{ch_num}"

        try:
            gpio_chip = gpiod.Chip(gpio_chip_device_name)
            print(f"Open {gpio_chip_device_name} Successfully")

        except OSError:
            print(f"Error opening GPIO chip: {gpio_chip_device_name}")          
    else:
        print('Error: Wrong GPIO device number input')

def GpioChipClose():

    global gpio_chip_device_name
    global gpio_chip

    if gpio_chip:

        gpio_chip.close()
        print(f"{gpio_chip_device_name} closed")

def GetChipLines():

    global gpio_chip_device_name
    global gpio_chip

    num_lines = gpio_chip.get_info().num_lines

    print(f"==========={gpio_chip_device_name} lines:{num_lines}===========")
    for i in range(num_lines):
        try:
            info = gpio_chip.get_line_info(i)     
            direction = info.direction.name if info.direction else "unknown"
            print(f"line{i}\t direction: {direction}")

        except Exception as e:
            print(f"Error to get: line{i} ({e})")
    print("=============================================")
    

def GpioChipSetOutput():
    
    try:
        
        global gpio_chip_device_name
        global gpio_chip

        print('GPIO chip line number  ?')
        line_num = input()

        if re.search('^[0-9]+$', line_num):

            print('Output value(0 or 1)?')
            value = input()
        
            if re.fullmatch(r'[01]', value):
                
                with gpio_chip.request_lines(config={line_num: gpiod.LineSettings(direction=Direction.OUTPUT, output_value=Value.INACTIVE)}, consumer='gpio_output') as request: 
                   if value=='1': 
                       request.set_value(line_num, Value.ACTIVE)
                       print(f'{gpio_chip_device_name} line{line_num} set value:1')

                   else:
                       request.set_value(line_num, Value.INACTIVE)
                       print(f'{gpio_chip_device_name} line{line_num} set value:0')
            else:
                print('Error: Wrong output value input')
        else:
            print('Error: Wrong line number input')
            
    except Exception as e:
        print(f"GPIO error: {e}")

def GpioChipGetInput():

    try:
        
        global gpio_chip_device_name
        global gpio_chip

        print('GPIO chip line number  ?')
        line_num = input()

        if re.search('^[0-9]+$', line_num):

            with gpio_chip.request_lines(config={line_num: gpiod.LineSettings(direction=Direction.INPUT)}, consumer='gpio_input') as request:
            
                value = request.get_value(line_num)
                           
                if value == Value.INACTIVE:    
                
                    print(f'{gpio_chip_device_name} line{line_num} get value:0')
                
                else:

                    print(f'{gpio_chip_device_name} line{line_num} get value:1')
        else:
                print('Error: Wrong line number input')
    except Exception as e:
        print(f"GPIO error: {e}")

def WaitInputEvent():

    try:
        
        global gpio_chip_device_name
        global gpio_chip

        print('GPIO chip line number  ?')
        line_num = input()

        if re.search('^[0-9]+$', line_num):
            
            with gpio_chip.request_lines(config={line_num: gpiod.LineSettings(direction=Direction.INPUT, edge_detection=Edge.BOTH)}, consumer='gpio_event') as request:
                
                global w_proc
                w_proc = multiprocessing.Process(target=EventWait, daemon=True, args=(request, line_num,)) # Multiprocessing replace threading, cus easy to terminate the process

                w_proc.start()
                print(f'Waiting line{line_num} event...press "esc" key to exit.')           
                keyboard.wait('esc')           
                w_proc.terminate()
        else:
                print('Error: Wrong line number input')
    
    except Exception as e:
        print(f"GPIO error: {e}")

def EventWait(request: LineRequest, line_num):
    
    while True:

        try:
        
            if request.wait_edge_events(timeout=0.5):

                event_list = request.read_edge_events(max_events=5)
                    
                for e in event_list:

                    if e.event_type == EdgeEvent.Type.RISING_EDGE:
                        print(f"line{line_num} rising edge triggered. Offset:{e.line_offset}, Timestamp:{e.timestamp_ns/1000000000}")
                
                    elif e.event_type == EdgeEvent.Type.FALLING_EDGE:
                        print(f"line{line_num} falling edge triggered. Offset:{e.line_offset}, Timestamp{e.timestamp_ns/1000000000}")
     

        except Exception as e:
            print(f"GPIO error: {e}")


def main():
    
    GpioTestList()

    while True: 

        print()
        print('CMD?')

        key = click.getchar()

        if(key == '1'):

            GpioChipOpen()
        
        elif(key == '2'):

            GpioChipClose()
        
        elif(key == '3'):

            GetChipLines()
        
        elif(key == '4'):

            GpioChipSetOutput()
        
        elif(key == '5'):

            GpioChipGetInput()
        
        elif(key == '6'):

            WaitInputEvent()

        elif(key == '?'):
                     
            GpioTestList()
            
        elif(key == 'Q'or  key == 'q'):
      
            quit()

        time.sleep(0.2)

if __name__ == '__main__':
    main()