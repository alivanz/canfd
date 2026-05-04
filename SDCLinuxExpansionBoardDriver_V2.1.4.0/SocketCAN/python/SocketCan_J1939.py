#! /usr/bin/python3
import j1939
import time
import os
import re
import datetime
import click

ecu = j1939.ElectronicControlUnit()
ch = None
is_opened = False


def on_message_callback(priority ,pgn, sa, timestamp, data):
     
    data_print = " ".join(["{0:02X}".format(b) for b in data])
    ts = datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S.%f') #micro seconds

    print(f'******************** Received J1939 Message **********************')
    print(f'Timestamp:{ts}')
    print(f'Priority:{priority:X}')
    print(f'Source Address:{sa:X}')
    print(f'PGN:{pgn}, hex format:{pgn:X}')
    print(f'Data:{data_print}')
    print('*******************************************************************')

def J1939TestList():
    os.system('clear')
    print('>------------------------------------<')
    print('1 : J1939 Socket Open')
    print('2 : J1939 Socket Close')
    print('3 : Send J1939 Message')
    print('4 : Receive J1939 Message')

    print()
    print('? : Command list')
    print('Q : Quit')
    print('>------------------------------------<')

def J1939Open():
    
    global ecu
    global ch
    global is_opened

    if(is_opened == False):

        print('Channel?(eg: "can0" -> input 0)')
        ch_num = input()

        if re.search('^[0-9]+$', ch_num):
        
            try:

                ch = "can{number}".format(number = ch_num)
        
                #open socket
                ecu.connect(bustype = 'socketcan', channel = ch)

                print(f'{ch} J1939 socket open sucess')

                is_opened = True
            
            except Exception as e:
        
                print(f'Error:{str(e)}')

                is_opened = False      
        else:
            print('Error: Wrong channel number input')
    else:
        print(f'Info: This program can only open one CAN port at a time, {ch} J1939 socket is opened')          
    
def J1939Close():
    
    global is_opened

    if(is_opened == True):
        
        try:
        
            ecu.disconnect()
            
            print(f'{ch} J1939 socket close sucess')

            is_opened = False

        except Exception as e:
        
            print(f'Error:{str(e)}')
    else:
        print(f'{ch} J1939 socket has been closed')

def SendJ1939Message():
    
    if(is_opened == True):

        print('PGN? (Hex format, range: 0x0 ~ 0x3ffff)')
        input_pgn = input()
    
        if re.search('^[0-9a-fA-F]+$', input_pgn):
        
            if(int(input_pgn, 16) <= 0x3ffff and int(input_pgn, 16) >= 0x0):

                print('Address(SA, Hex format, 1 byte)?')
                sa = input()

                if re.search('^[0-9a-fA-F]+$', sa):

                    if (int(sa, 16) <= 0xff and int(sa, 16) >= 0x0):
                
                        print('Data? <= 8bytes(e.g. 12 34 56 78 90 AB CD EF)')
                        input_data = input()
                
                        if re.search('^(?:[0-9A-Fa-f]{2} ){0,7}([0-9A-Fa-f]{2})$', input_data):
                    

                            #************************************************
                            #**29 bit extended CAN-ID************************
                            #**[Priority]   [PGN]       [SA(Source Address)]*
                            #**[Bit 28...26][Bit 25...8][Bit 7...0]**********
                            #************************************************

                            #****************************************************************
                            #*PGN************************************************************
                            # [R(Reserver)][DP(Data Page)][PF(PDU Format)][PS[PDU Specific]]
                            # [Bit25]      [Bit24]        [Bit 23...16]   [Bit 15...8]
                            #****************************************************************

                            if(int(input_pgn, 16) <= 0xff and int(input_pgn, 16) >= 0x0):
                            
                                ps = int(input_pgn, 16) & 0xff
                                pf = 0
                                dp = 0
                            
                            elif(int(input_pgn, 16) <= 0xfff and int(input_pgn, 16) >= 0x100):

                                ps = int(input_pgn, 16) & 0xff
                                pf = int((int(input_pgn, 16) & 0xf00) / 0x100)
                                dp = 0

                            elif(int(input_pgn, 16) <= 0xffff and int(input_pgn, 16) >= 0x1000):

                                ps = int(input_pgn, 16) & 0xff
                                pf = int((int(input_pgn, 16) & 0xff00) / 0x100)
                                dp = 0
                            
                            elif(int(input_pgn, 16) <= 0x3ffff and int(input_pgn, 16) >= 0x10000):

                                ps = int(input_pgn, 16) & 0xff
                                pf = int((int(input_pgn, 16) & 0xff00) / 0x100)
                                dp = int((int(input_pgn, 16) & 0xf0000) / 0x10000)


                            InpDataSplite = input_data.split()
                        
                            DataList = []

                            for i in InpDataSplite:
                            
                                DataList.append(int(i,16))
                    
                            DataListBytes = bytes(DataList)
                        
                            try:

                                result = ecu.send_pgn(data_page=dp, pdu_format= pf, pdu_specific= ps, priority=6, src_address=int(sa, 16), data=DataListBytes, time_limit= 0.1)

                                if(result == False):
                        
                                    print(f'Error: {ch} Send J1939 message fail, return: {result}')
                            except Exception as e:
        
                                print(f'Error:{str(e)}')
                        else:
                            print('Error: Wrong data format input')
                    else:
                        print('Error: Out of the SA range')
                else:
                    print('Error: Wrong SA format input')
            else:
                print('The PGN input out of range')
        else:
            print('Error: Wrong PGN format input')
    else:
        print(f'Info: No net device is opened')

def ReceiveJ1939Message():
    
    if(is_opened == True):
        
        try:
            # subscribe to all (global) messages on the bus
            ecu.subscribe(on_message_callback)

            print(f'{ch} waitng for J1939 message ...')

        except Exception as e:

            print(f'Error:{str(e)}')
    else:
        print(f'Info: No net device is opened')

    
def main():

    J1939TestList()

    while True:
        
        print()
        print('CMD?')

        key = click.getchar()
      
        if(key == '1'):
            
            J1939Open()
        
        elif(key == '2'):
            
            J1939Close()
        
        elif(key == '3'):

            SendJ1939Message()

        elif(key == '4'):
            
            ReceiveJ1939Message()
        
        elif(key == '?'):
      
            J1939TestList()

        elif(key == 'Q'or  key == 'q'):
      
            quit()

        time.sleep(0.2)


if __name__ == '__main__':
      main()