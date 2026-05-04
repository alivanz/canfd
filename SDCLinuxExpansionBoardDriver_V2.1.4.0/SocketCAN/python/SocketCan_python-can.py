#! /usr/bin/python3
from pickle import NONE
import can
import time
import os
import keyboard
import re
import datetime
import multiprocessing
import asyncio
import secrets
import click

r_proc = None
is_listening = False

def SocketCanTestList():
      
      os.system('clear')
      print('>------------------------------------<')
      print('1 : Send a CAN frame')
      print('2 : Send a CAN FD frame')
      print('3 : Send a Random CAN frame')
      print('4 : Send a Random CAN FD frame')
      print('5 : Send period CAN frame')
      print('6 : Send period CAN FD frame')
      print('7 : Send period CAN frame(Random data)')
      print('8 : Send period CAN FD frame(Random Data)')
      print('9 : Receive CAN frame')
      print('0 : Receive CAN frame with filter')
      print('A : Receive CAN frame by listener(BufferedReader)')
      print('B : Receive CAN frame by listener(AsyncBufferedReader)')
      print('C : End the listener')
      
      print()
      print('? : Command list')
      print('Q : Quit')
      print('>------------------------------------<')

def SendFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):

            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                print('Data? <= 8bytes(e.g. 12 34 56 78 90 AB CD EF)')
                input_data = input()

                if re.search('^(?:[0-9A-Fa-f]{2} ){0,7}([0-9A-Fa-f]{2})$', input_data):
        
                    ch = "can{number}".format(number = ch_num)

                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)

                    InpDataSplite = input_data.split()
                        
                    DataList = []

                    for i in InpDataSplite:
                            
                        DataList.append(int(i,16))
                        
                    DataListBytes = bytes(DataList)

                    if(int(id, 16) > 0x7FF):                   
                        msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=True, is_fd=False)
                    else:
                        msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=False, is_fd=False)
                
                    try:
                        
                        bus.send(msg)
                        data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                                                
                        print(f'******************* Message Sent ********************')
                        print(f'Timestamp:{msg.timestamp}')
                        print(f'Channel:{msg.channel}')
                        print(f'ID:{hex(msg.arbitration_id)}')
                        print(f'DLC:{msg.dlc}')
                        print(f'Data:{data_print}')
                        print(f'FD:{msg.is_fd}')
                        print('*************************************************************')       
                    
                    except can.CanError as e:                   
                        
                        print(f'CAN Error:{str(e)}')
                else:
                    print('Error: Wrong data format input')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input')                   
    else:
        print('Error: Wrong channel number input')

def SendFdFrame():
        
    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):

            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                print('Data? <= 64bytes(e.g. 12 34 56 78 90 AB CD EF 12 34 56 78 90 AB CD EF)')
                input_data = input()

                if re.search('^(?:[0-9A-Fa-f]{2} ){0,63}([0-9A-Fa-f]{2})$', input_data):
        
                    ch = "can{number}".format(number = ch_num)

                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)

                    InpDataSplite = input_data.split()
                        
                    DataList = []

                    for i in InpDataSplite:
                            
                        DataList.append(int(i,16))
                        
                    DataListBytes = bytes(DataList)

                    if(int(id, 16) > 0x7FF):
                        msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=True, is_fd=True, bitrate_switch=True)
                    else:
                        msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=False, is_fd=True, bitrate_switch=True)
                    
                    try:
                        bus.send(msg)
                        data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                        
                        print(f'Send Message, Timestamp:{msg.timestamp}, ID:{msg.arbitration_id}, Data:{data_print} FD:{msg.is_fd} BRS:{msg.bitrate_switch} ESI:{msg.error_state_indicator}')

                        print(f'******************** Message Sent **********************')
                        print(f'Timestamp:{msg.timestamp}')
                        print(f'Channel:{msg.channel}')
                        print(f'ID:{hex(msg.arbitration_id)}')
                        print(f'DLC:{msg.dlc}')
                        print(f'Data:{data_print}')
                        print(f'FD:{msg.is_fd}')
                        print(f'BRS:{msg.bitrate_switch}')
                        print(f'ESI:{msg.error_state_indicator}')
                        print('*************************************************************')   

                    except can.CanError as e:                   
                        print(f'CAN Error:{str(e)}')
                else:
                    print('Error: Wrong data format input')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input')                   
    else:
        print('Error: Wrong channel number input')

def SendRandomFrame():

   print('Channel?(eg: "can0" -> input 0)')
   ch_num = input()

   if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):

            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                ch = "can{number}".format(number = ch_num)

                bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)

                DataLen = secrets.randbelow(8)

                DataBytes = secrets.token_bytes(DataLen)

                if(int(id, 16) > 0x7FF):                   
                    msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=True, is_fd=False)
                else:
                    msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=False, is_fd=False)
                
                try:
                        
                    bus.send(msg)
                    data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                                                
                    print(f'******************* Message Sent ********************')
                    print(f'Timestamp:{msg.timestamp}')
                    print(f'Channel:{msg.channel}')
                    print(f'ID:{hex(msg.arbitration_id)}')
                    print(f'DLC:{msg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{msg.is_fd}')
                    print('*************************************************************')       
                    
                except can.CanError as e:                   
                        
                    print(f'CAN Error:{str(e)}')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input')                   
   else:
        print('Error: Wrong channel number input')

def SendRandomFdFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):

            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                ch = "can{number}".format(number = ch_num)

                bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)

                DataLen = secrets.randbelow(64)

                DataBytes = secrets.token_bytes(DataLen)

                if(int(id, 16) > 0x7FF):
                    msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=True, is_fd=True, bitrate_switch=True)
                else:
                    msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=False, is_fd=True, bitrate_switch=True)
                    
                try:
                    bus.send(msg)
                    data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                        
                    print(f'Send Message, Timestamp:{msg.timestamp}, ID:{msg.arbitration_id}, Data:{data_print} FD:{msg.is_fd} BRS:{msg.bitrate_switch} ESI:{msg.error_state_indicator}')

                    print(f'******************** Message Sent **********************')
                    print(f'Timestamp:{msg.timestamp}')
                    print(f'Channel:{msg.channel}')
                    print(f'ID:{hex(msg.arbitration_id)}')
                    print(f'DLC:{msg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{msg.is_fd}')
                    print(f'BRS:{msg.bitrate_switch}')
                    print(f'ESI:{msg.error_state_indicator}')
                    print('*************************************************************')   

                except can.CanError as e:                   
                        print(f'CAN Error:{str(e)}')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input')                   
    else:
        print('Error: Wrong channel number input')

def SendPeriodFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()
    
    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):
        
            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                print('Data? <= 8bytes(e.g. 12 34 56 78 90 AB CD EF)')
                input_data = input()

                if re.search('^(?:[0-9A-Fa-f]{2} ){0,7}([0-9A-Fa-f]{2})$', input_data):

                    print('Period ?(period in seconds between each message)')
                    period = input()

                    if re.search('^[0-9][\.\d]*(,\d+)?$',period):

                        print('Duration ?(in seconds to continue sending messages)')
                        duration = input()
                        
                        if re.search('^[0-9][\.\d]*(,\d+)?$',duration):
                
                            ch = "can{number}".format(number = ch_num)

                            bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)

                            InpDataSplite = input_data.split()
                        
                            DataList = []

                            for i in InpDataSplite:
                            
                                DataList.append(int(i,16))
                        
                            DataListBytes = bytes(DataList)

                            if(int(id, 16) > 0x7FF):                   
                                msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=True, is_fd=False)
                            else:                       
                                msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=False, is_fd=False)
                            
                            try:
                        
                                bus.send_periodic(msg, float(period), float(duration))
                                data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                                                
                                print(f'******************* Period Message Sent ********************')
                                print(f'Timestamp:{msg.timestamp}')
                                print(f'Period:{period}sec')
                                print(f'Duration:{duration}sec')
                                print(f'Channel:{msg.channel}')
                                print(f'ID:{hex(msg.arbitration_id)}')
                                print(f'DLC:{msg.dlc}')
                                print(f'Data:{data_print}')
                                print(f'FD:{msg.is_fd}')
                                print('*************************************************************')       
                    
                            except can.CanError as e:                   
                        
                                print(f'CAN Error:{str(e)}')
                        else:
                            print('Error: Wrong duration input')           
                    else:
                        print('Error: Wrong period input')   
                else:
                    print('Error: Wrong data format input')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input') 
    else:
        print('Error: Wrong channel number input')

def SendPeriodFdFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()
    
    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):
        
            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):

                print('Data? <= 64bytes(e.g. 12 34 56 78 90 AB CD EF 12 34 56 78 90 AB CD EF)')
                input_data = input()

                if re.search('^(?:[0-9A-Fa-f]{2} ){0,63}([0-9A-Fa-f]{2})$', input_data):

                    print('Period ?(period in seconds between each message)')
                    period = input()

                    if re.search('^[0-9][\.\d]*(,\d+)?$',period):

                        print('Duration ?(in seconds to continue sending messages)')
                        duration = input()
                        
                        if re.search('^[0-9][\.\d]*(,\d+)?$',duration):
                
                            ch = "can{number}".format(number = ch_num)

                            bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)

                            InpDataSplite = input_data.split()
                        
                            DataList = []

                            for i in InpDataSplite:
                            
                                DataList.append(int(i,16))
                        
                            DataListBytes = bytes(DataList)

                            if(int(id, 16) > 0x7FF):                   
                                msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=True, is_fd=True, bitrate_switch=True)
                            else:                       
                                msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataListBytes, is_extended_id=False, is_fd=True, bitrate_switch=True)
                            
                            try:
                        
                                bus.send_periodic(msg, float(period), float(duration))
                                data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                                                
                                print(f'******************** Period Message Sent **********************')
                                print(f'Timestamp:{msg.timestamp}')
                                print(f'Period:{period}sec')
                                print(f'Duration:{duration}sec')
                                print(f'Channel:{msg.channel}')
                                print(f'ID:{hex(msg.arbitration_id)}')
                                print(f'DLC:{msg.dlc}')
                                print(f'Data:{data_print}')
                                print(f'FD:{msg.is_fd}')
                                print(f'BRS:{msg.bitrate_switch}')
                                print(f'ESI:{msg.error_state_indicator}')
                                print('*************************************************************')      
                    
                            except can.CanError as e:                   
                        
                                print(f'CAN Error:{str(e)}')
                        else:
                            print('Error: Wrong duration input')           
                    else:
                        print('Error: Wrong period input')   
                else:
                    print('Error: Wrong data format input')
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input') 
    else:
        print('Error: Wrong channel number input')

def SendPeriodRandomFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):
        
            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):
            
                print('Period ?(period in seconds between each message)')
                period = input()

                if re.search('^[0-9][\.\d]*(,\d+)?$',period):

                    print('Duration ?(in seconds to continue sending messages)')
                    duration = input()
                        
                    if re.search('^[0-9][\.\d]*(,\d+)?$',duration):
            
                        DataLen = secrets.randbelow(8)

                        DataBytes = secrets.token_bytes(DataLen)

                        ch = "can{number}".format(number = ch_num)

                        bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)

                        if(int(id, 16) > 0x7FF):
                            msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=True, is_fd=False)
                        else:
                            msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=False, is_fd=False)

                        try:

                            bus.send_periodic(msg, float(period), float(duration))
                            data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                        
                            print(f'Send Message, Timestamp:{msg.timestamp}, ID:{msg.arbitration_id}, Data:{data_print} FD:{msg.is_fd}')

                            print(f'******************** Message Sent **********************')
                            print(f'Timestamp:{msg.timestamp}')
                            print(f'Channel:{msg.channel}')
                            print(f'ID:{hex(msg.arbitration_id)}')
                            print(f'DLC:{msg.dlc}')
                            print(f'Data:{data_print}')
                            print(f'FD:{msg.is_fd}')
                            print('*************************************************************')   
                
                        except can.CanError as e:                   
                            print(f'CAN Error:{str(e)}')
                    else:
                        print('Error: Wrong duration input')      
                else:
                    print('Error: Wrong period input')   
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input') 
    else:
        print('Error: Wrong channel number input')
 

def SendPeriodRandomFdFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('ID?(Base:11bits; Extended:29bits)')
        id = input()

        if re.search('^[0-9a-fA-F]+$', id):
        
            if (int(id, 16) <= 0x1FFFFFFF and int(id, 16) >= 0x0):
            
                print('Period ?(period in seconds between each message)')
                period = input()

                if re.search('^[0-9][\.\d]*(,\d+)?$',period):

                    print('Duration ?(in seconds to continue sending messages)')
                    duration = input()
                        
                    if re.search('^[0-9][\.\d]*(,\d+)?$',duration):
            
                        DataLen = secrets.randbelow(64)

                        DataBytes = secrets.token_bytes(DataLen)

                        ch = "can{number}".format(number = ch_num)

                        bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)

                        if(int(id, 16) > 0x7FF):
                            msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=True, is_fd=True, bitrate_switch=True)
                        else:
                            msg = can.Message(timestamp= datetime.datetime.fromtimestamp(time.time()), arbitration_id=int(id, 16), data = DataBytes, is_extended_id=False, is_fd=True, bitrate_switch=True)

                        try:

                            bus.send_periodic(msg, float(period), float(duration))
                            data_print = " ".join(["{0:02X}".format(b) for b in msg.data])
                        
                            print(f'Send Message, Timestamp:{msg.timestamp}, ID:{msg.arbitration_id}, Data:{data_print} FD:{msg.is_fd} BRS:{msg.bitrate_switch} ESI:{msg.error_state_indicator}')

                            print(f'******************** Message Sent **********************')
                            print(f'Timestamp:{msg.timestamp}')
                            print(f'Channel:{msg.channel}')
                            print(f'ID:{hex(msg.arbitration_id)}')
                            print(f'DLC:{msg.dlc}')
                            print(f'Data:{data_print}')
                            print(f'FD:{msg.is_fd}')
                            print(f'BRS:{msg.bitrate_switch}')
                            print(f'ESI:{msg.error_state_indicator}')
                            print('*************************************************************')   
                
                        except can.CanError as e:                   
                            print(f'CAN Error:{str(e)}')
                    else:
                        print('Error: Wrong duration input')      
                else:
                    print('Error: Wrong period input')   
            else:
                print('Error: Out of the ID range')
        else:
            print('Error: Wrong ID format input') 
    else:
        print('Error: Wrong channel number input')

def ReceiveFrame():

    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):

        print('CAN2.0/FD?(0:CAN 2.0; 1:CAN FD)')
        IsFd = input()

        if re.search('^[0-1]+$', IsFd):
    
            ch = "can{number}".format(number = ch_num)

            if(bool(IsFd)):
                bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)
            else:
                bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)
                            
            r_proc = multiprocessing.Process(target= CanRecv, daemon=True, args=(bus,)) # Multiprocessing replace threading, cus easy to terminate the process
            r_proc.start()
            print('Start receiving data, press "esc" key to end receive mode.')           
            keyboard.wait('esc')           
            r_proc.terminate()

        else:
            print('Error: Wrong CAN2.0/FD input')
    else:
        print('Error: Wrong channel number input')

def ReceiveFrameWithFilter():

    print('Tip: Socket can filter formula: received_can_id & mask = code_id & mask')
    print('Channel?(eg: "can0" -> input 0)')
    ch_num = input()

    if re.search('^[0-9]+$', ch_num):
        
        print('CAN2.0/FD?(0:CAN 2.0; 1:CAN FD)')
        IsFd = input()
    
        if re.search('^[0-1]+$', IsFd):

            print('Set a base code ID for base filter?')
            BaseCode = input()

            if (re.search('^[0-9a-fA-F]+$', BaseCode) and int(BaseCode, 16) <= 0x7ff and int(BaseCode, 16) >= 0x0):

                print('Set a base mask for base filter?')
                BaseMask = input()

                if (re.search('^[0-9a-fA-F]+$', BaseMask) and int(BaseMask, 16) <= 0x7ff and int(BaseMask, 16) >= 0x0):

                    print('Set a extended code ID for extended filter?')
                    ExtCode = input()
                    
                    if (re.search('^[0-9a-fA-F]+$', ExtCode) and int(ExtCode, 16) <= 0x1FFFFFFF and int(ExtCode, 16) >= 0x0):

                        print('Set a extended mask for extended filter?')
                        ExtMask = input()

                        if (re.search('^[0-9a-fA-F]+$', ExtMask) and int(ExtMask, 16) <= 0x1FFFFFFF and int(ExtMask, 16) >= 0x0):
                            
                            ch = "can{number}".format(number = ch_num)

                            filters = [
                                        {"can_id": int(BaseCode, 16), "can_mask": int(BaseMask, 16), "extended": False},
                                        {"can_id": int(ExtCode, 16), "can_mask": int(ExtMask, 16), "extended": True}
                                        ]
                            if(bool(IsFd)): 
                                bus = can.interface.Bus(interface = 'socketcan', channel=ch, can_filters= filters, fd = True)
                            else:
                                bus = can.interface.Bus(interface = 'socketcan', channel=ch, can_filters= filters, fd = False)

                            r_proc = multiprocessing.Process(target= CanRecv, daemon=True, args=(bus,)) # Multiprocessing replace threading, cus easy to terminate the process
                            r_proc.start()
                            print('Start receiving data, press "esc" key to end receive mode.')           
                            keyboard.wait('esc')           
                            r_proc.terminate()
                        else:
                            print('Error: Wrong extended mask input')
                    else:
                        print('Error: Wrong extended code ID input')
                else:
                    print('Error: Wrong base mask input')
            else:
                print('Error: Wrong base code ID input')
        else:
            print('Error: Wrong CAN2.0/FD input')
    else:
        print('Error: Wrong channel number input')

def ReceiveFrameByListener():

    global is_listening

    if(not is_listening):
        print('Channel?(eg: "can0" -> input 0)')
        ch_num = input()

        if re.search('^[0-9]+$', ch_num):
        
            print('CAN2.0/FD?(0:CAN 2.0; 1:CAN FD)')
            IsFd = input()

            if re.search('^[0-1]+$', IsFd):
                       
                ch = "can{number}".format(number = ch_num)

                if(bool(IsFd)):
                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)
                else:
                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)
               
                global r_proc
                r_proc = multiprocessing.Process(target= CanListen, daemon=True, args=(bus,)) # Multiprocessing replace threading, cus easy to terminate the process
            
                r_proc.start()
                is_listening = True
                print(f'{ch} start listening...')                                  
            else:
                print('Error: Wrong CAN2.0/FD input')
        else:
            print('Error: Wrong channel number input')
    else:
        print('Error: Listener has now been started')

def ReceiveFrameByAsynListener():

    global is_listening

    if(not is_listening):
        print('Channel?(eg: "can0" -> input 0)')
        ch_num = input()

        if re.search('^[0-9]+$', ch_num):

            print('CAN2.0/FD?(0:CAN 2.0; 1:CAN FD)')
            IsFd = input()

            if re.search('^[0-1]+$', IsFd):
                ch = "can{number}".format(number = ch_num)

                if(bool(IsFd)):
                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = True)
                else:
                    bus = can.interface.Bus(interface = 'socketcan', channel=ch, fd = False)
            
                global r_proc
                r_proc = multiprocessing.Process(target= asyncio.run, daemon=True, args=(AsyncCanListen(bus),)) # Multiprocessing replace threading, cus easy to terminate the process
                r_proc.start()
                is_listening = True
                print(f'{ch} start asynchronous listening ...')

            else:
                print('Error: Wrong CAN2.0/FD input')
        else:
            print('Error: Wrong channel number input')
    else:
        print('Error: Listener has now been started')

def CanRecv(bus: can.interface.Bus):

    while True:

        try:        
            RevMsg = bus.recv()
 
            data_print = " ".join(["{0:02X}".format(b) for b in RevMsg.data])
            #ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3] #milli seconds
            ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f') #micro seconds

            if(RevMsg.is_fd):
                print(f'******************** Received Message **********************')
                print(f'Timestamp:{ts}')
                print(f'Channel:{RevMsg.channel}')
                print(f'ID:{hex(RevMsg.arbitration_id)}')
                print(f'DLC:{RevMsg.dlc}')
                print(f'Data:{data_print}')
                print(f'FD:{RevMsg.is_fd}')
                print(f'BRS:{RevMsg.bitrate_switch}')
                print(f'ESI:{RevMsg.error_state_indicator}')
                print('*************************************************************')      
            else:
                print(f'******************** Received Message **********************')
                print(f'Timestamp:{ts}')
                print(f'Channel:{RevMsg.channel}')
                print(f'ID:{hex(RevMsg.arbitration_id)}')
                print(f'DLC:{RevMsg.dlc}')
                print(f'Data:{data_print}')
                print(f'FD:{RevMsg.is_fd}')
                print('*************************************************************')     
            
            print('')
            print('************************************************************')
            print('*********press "esc" key to end the receive mode************')
            print('************************************************************')
            print('')

        except can.CanError as e:                   
            print(f'CAN Error:{str(e)}')

def CanListen(bus):
    
    RxBuff = can.listener.BufferedReader()
    can.notifier.Notifier(bus, [RxBuff])

    while True:
      
        try:
            RevMsg = RxBuff.get_message()

            if(RevMsg != None):
            
                data_print = " ".join(["{0:02X}".format(b) for b in RevMsg.data])
                #ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3] #milli seconds
                ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f') #micro seconds
                if(RevMsg.is_fd):
                    print(f'******************* Message Notification ********************')
                    print(f'Timestamp:{ts}')
                    print(f'Channel:{RevMsg.channel}')
                    print(f'ID:{hex(RevMsg.arbitration_id)}')
                    print(f'DLC:{RevMsg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{RevMsg.is_fd}')
                    print(f'BRS:{RevMsg.bitrate_switch}')
                    print(f'ESI:{RevMsg.error_state_indicator}')
                    print('*************************************************************')                    
                else:
                    print(f'******************* Message Notification ********************')
                    print(f'Timestamp:{ts}')
                    print(f'Channel:{RevMsg.channel}')
                    print(f'ID:{hex(RevMsg.arbitration_id)}')
                    print(f'DLC:{RevMsg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{RevMsg.is_fd}')
                    print('*************************************************************')
        except can.CanError as e:                   
                print(f'CAN Error:{str(e)}')        

async def AsyncCanListen(bus: can.interface.Bus):

    RxBuff = can.listener.AsyncBufferedReader()
    loop = asyncio.get_running_loop()
    can.notifier.Notifier(bus, [RxBuff], loop=loop)
  
    while True:
        try:
            RevMsg = await RxBuff.get_message()
            
            if(RevMsg != None):
            
                data_print = " ".join(["{0:02X}".format(b) for b in RevMsg.data])
                #ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f')[:-3] #milli seconds
                ts = datetime.datetime.fromtimestamp(RevMsg.timestamp).strftime('%Y-%m-%d %H:%M:%S.%f') #micro seconds
                if(RevMsg.is_fd):
                    print(f'******************* Async Message Notification ********************')
                    print(f'Timestamp:{ts}')
                    print(f'Channel:{RevMsg.channel}')
                    print(f'ID:{hex(RevMsg.arbitration_id)}')
                    print(f'DLC:{RevMsg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{RevMsg.is_fd}')
                    print(f'BRS:{RevMsg.bitrate_switch}')
                    print(f'ESI:{RevMsg.error_state_indicator}')
                    print('********************************************************************')                    
                else:
                    print(f'******************* Async Message Notification ********************')
                    print(f'Timestamp:{ts}')
                    print(f'Channel:{RevMsg.channel}')
                    print(f'ID:{hex(RevMsg.arbitration_id)}')
                    print(f'DLC:{RevMsg.dlc}')
                    print(f'Data:{data_print}')
                    print(f'FD:{RevMsg.is_fd}')
                    print('********************************************************************')  
        except can.CanError as e:                   
            print(f'CAN Error:{str(e)}')    

def EndListen():
    
    if(r_proc != None):
        r_proc.terminate()
        print(f'End listening...')

def main():

    SocketCanTestList()

    while True: 
        
        print()
        print('CMD?')

        key = click.getchar()
      
        if(key == '1'):

            SendFrame()

        elif(key == '2'):

            SendFdFrame()
        
        elif(key == '3'):

            SendRandomFrame()
        
        elif(key == '4'):

            SendRandomFdFrame()
    
        elif(key == '5'):

            SendPeriodFrame()
    
        elif(key == '6'):

            SendPeriodFdFrame()
        
        elif(key == '7'):

            SendPeriodRandomFrame()
            
        elif(key == '8'):
            
            SendPeriodRandomFdFrame()

        elif(key == '9'):

            ReceiveFrame()
      
        elif(key == '0'):

            ReceiveFrameWithFilter()
      
        elif(key == 'A'):

            ReceiveFrameByListener()
    
        elif(key == 'B'):
        
            ReceiveFrameByAsynListener()
                
        elif(key == 'C'):
        
            if(is_listening):
                EndListen()
                is_listening = False
            else:
                print('listener is now closed')
                
        elif(key == '?'):
      
            SocketCanTestList()

        elif(key == 'Q'or  key == 'q'):
      
            quit()

        time.sleep(0.2)

if __name__ == '__main__':
      main()
