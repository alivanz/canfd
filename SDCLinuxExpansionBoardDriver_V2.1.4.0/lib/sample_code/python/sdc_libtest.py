#! /usr/bin/python3
import argparse
import ctypes
import logging
from pickle import NONE
import threading
import time
import os
import keyboard
import re
import platform
import distro
import click

from ctypes import *

sdc = cdll.LoadLibrary('/usr/lib/libsdc.so.2.0.0')

SDC_DIO_BANK_MAX = 32

STATUS_SUCCESS = 0
STATUS_INVALID_PARAMETER = -1
STATUS_DEVICE_BUSY = -2
STATUS_NO_SUCH_DEVICE = -3
STATUS_ALLOC_MEMORY_FAIL = -4
STATUS_CREATE_THREAD_FAIL = -5
STATUS_IOCTL_FAIL = -6
STATUS_CONTROLLER_VERSION_UNSUPPORT = -7
STATUS_DIO_BANK_NOT_FOUND = -101
STATUS_DIO_BANK_PORT_NOT_FOUND = -102
STATUS_DIO_BANK_NO_INPUT_CAP = -103
STATUS_DIO_BANK_NO_OUTPUT_CAP = -104

class BankInfoStruct(Structure):
      _fields_ = [("nr_port", c_int),
                  ("cap_input", c_int),
                  ("cap_output", c_int),
                  ("cap_rising_trigger", c_int),
                  ("cap_falling_trigger", c_int)]
                  #Totol Size = 20

class BankInfo():
      def __init__(self, nr_port, cap_input, cap_output, cap_rising_trigger, cap_falling_trigger):
            self.nr_port = nr_port
            self.cap_input = cap_input
            self.cap_output = cap_output
            self.cap_rising_trigger =  cap_rising_trigger
            self.cap_falling_trigger =  cap_falling_trigger

class DioInfoStruct(Structure):
    
      fields_= [("pci_bus", c_int), 
                ("irq", c_int),
                ("line", c_int),
                ("index", c_int),
                ("version", c_int),
                ("cap_samedirection", c_int),
                ("cap_storeflash", c_int),
                ("nr_bank", c_int),
                ("di_sampling_freq", c_int),
                ("di_filter_lower_bound_ms", c_int),
                ("di_filter_min_ms", c_int),
                ("di_filter_max_ms", c_int),
                ("BankInfoList", BankInfoStruct * SDC_DIO_BANK_MAX)] #640
                #Totol Size = 688


class DioInfo():

      def __init__(self, bDioInfo):
            
            bPciBus = bDioInfo[0:4]         
            self.pci_bus = int.from_bytes(bPciBus, byteorder='little')

            bIRQ = bDioInfo[4:8]
            self.irq = int.from_bytes(bIRQ, byteorder='little')

            bLine = bDioInfo[8:12]
            self.line = int.from_bytes(bLine, byteorder='little')

            bIndex = bDioInfo[12:16]
            self.index = int.from_bytes(bIndex, byteorder='little')

            bVersion =bDioInfo[16:20]
            self.version = int.from_bytes(bVersion, byteorder='little')

            bCapSameDirection = bDioInfo[20:24]
            self.cap_samedirection = int.from_bytes(bCapSameDirection, byteorder='little')
            
            bStoreFlash = bDioInfo[24:28]
            self.cap_storeflash = int.from_bytes(bStoreFlash, byteorder='little')

            bNrBank = bDioInfo[28:32]
            self.nr_bank = int.from_bytes(bNrBank, byteorder='little')

            bDiSamplingFreq = bDioInfo[32:36]
            self.di_sampling_freq = int.from_bytes(bDiSamplingFreq, byteorder='little')

            bDiFilterLowerBoundMs = bDioInfo[36:40]
            self.di_filter_lower_bound_ms = int.from_bytes(bDiFilterLowerBoundMs, byteorder='little')

            bDiFilterMinMs = bDioInfo[40:44]
            self.di_filter_min_ms = int.from_bytes(bDiFilterMinMs, byteorder='little')

            bdiFilterMaxMs = bDioInfo[44:48]
            self.di_filter_max_ms = int.from_bytes(bdiFilterMaxMs, byteorder='little')

            bBankinfo = bDioInfo[48:688]

            self.bankinfo = []

            BankBase = 0
            BankSize = 20

            for i in range(self.nr_bank):
                  self.bankinfo.append(BankInfo(bBankinfo[0 + BankBase], bBankinfo[4 + BankBase], bBankinfo[8 + BankBase], bBankinfo[12 + BankBase], bBankinfo[16 + BankBase]))
                  BankBase += BankSize

def SdcDioTestList():
      
      os.system('clear')
      print('>------------------------------------<')
      print('1 : DIO Open')
      print('2 : DIO Close')
      print('A : Get DIO Information')
      print('B : Set DIO Bank State')
      print('C : Get DIO Bank State')
      print('D : Set input rising/falling event control ')
      print('E : Get input rising/falling event control')
      print('F : Register input event callback function')
      print('G : Unregister input event callback function')
      
      print()
      print('? : Command list')
      print('Q : Quit')
      print()
      print('Sample Version: V2.0.0.0')
      print('>------------------------------------<')

def SdcInputEventCallbackFunc(line ,BankIndex, InputValue, InputDelta):

      print('********  Input Event Triggered  *******')
      print(f'Line:{line}')
      print(f'Bank Index:{BankIndex}')
      print(f'Input Delta:0x{"%08x" % InputDelta}')
      print(f'Input Value:0x{"%08x" % InputValue}')
      print('****************************************')

def SdcDioOpen():
      
      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):
            
            nReturn = sdc.sdc_dio_open(int(line))

            print(f'sdc_dio_open return code:{"%d" % nReturn}')
            print()

            if(nReturn == STATUS_SUCCESS):
                  print('###################################################')
                  print('sdc_dio_open ok')
                  print('###################################################')
            else:
                  print('###################################################')
                  print('sdc_dio_open fail')
                  print('###################################################')
      else:
            print(f'line:{line} is invalid parameter')

def SdcDioClose():

      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):

            nReturn = sdc.sdc_dio_close(int(line))

            print(f'sdc_dio_close return code:{"%d" % nReturn}')
            print()

            if(nReturn == STATUS_SUCCESS):
                  print('###################################################')
                  print('sdc_dio_close ok')
                  print('###################################################')
            else:
                  print('###################################################')
                  print('sdc_dio_close fail')
                  print('###################################################')
      else:
            print(f'line:{line} is invalid parameter')

def SdcDumpDioInfo():

      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):

            DioInfoSize = 688

            DioInfoBuffer = (c_ubyte * DioInfoSize)()

            pDioInfoBuffer = pointer(DioInfoBuffer)

            pDioInfo = cast(pDioInfoBuffer, POINTER(DioInfoStruct))

            nReturn = sdc.sdc_dio_get_info(int(line), pDioInfo)
            
            print(f'sdc_dio_get_info return code:{"%d" % nReturn}')

            if(nReturn == STATUS_SUCCESS):
                  dioinfo = DioInfo(DioInfoBuffer)

                  print('###################################################')
                  print(f'PCI Bus Number: {dioinfo.pci_bus}')
                  print(f'IRQ: {dioinfo.irq}')
                  print(f'Line: {dioinfo.line}')
                  print(f'Index: {dioinfo.index}')
                  print(f'Version: {dioinfo.version}')
                  print(f'Same direction capability: {dioinfo.cap_samedirection}')
                  print(f'Store flash capability: {dioinfo.cap_storeflash}')
                  print(f'Number of bank: {dioinfo.nr_bank}')
                  print(f'DI sampling frequency(Hz): {dioinfo.di_sampling_freq}')
                  print(f'DI sampling lower bound value(ms): {dioinfo.di_filter_lower_bound_ms}')
                  print(f'DI sampling min value(ms): {dioinfo.di_filter_min_ms}')
                  print(f'DI sampling max value(ms): {dioinfo.di_filter_max_ms}')

                  for i in range(dioinfo.nr_bank):
                        print(f'===============Bank:{i}===============')
                        print(f'DIO Amount:{dioinfo.bankinfo[i].nr_port}')

                        if(dioinfo.bankinfo[i].cap_input == 1):
                              print('DIO Type: Input')
                        if(dioinfo.bankinfo[i].cap_output == 1):
                              print('DIO Type: Output')

                        print(f'DI event rising trigger capability:{dioinfo.bankinfo[i].cap_rising_trigger}')
                        print(f'DI event falling trigger capability:{dioinfo.bankinfo[i].cap_falling_trigger}')

                  print('###################################################')
            else:
                  print('###################################################')
                  print('sdc_dio_get_info fail')
                  print('###################################################')
      else:
            print(f'line:{line} is invalid parameter')

def SdcSetDioState():

      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):

            print('Bank Index?( 0:input / 1:output )')
            BankIndex = input()
            
            if (re.search('^[0-9]+$', BankIndex)): 

                  print('State?(4 byte hex value, e.g. 000000FF):')
                  State = input()

                  if((len(State) == 8) and (re.search('^[0-9a-fA-F]+$', State))):
                        
                        nReturn = sdc.sdc_dio_set_bank_state(int(line), int(BankIndex), int(State,16))
                        
                        if(nReturn == STATUS_SUCCESS):
                              print(f'#Set Bank:{BankIndex} State:0x{"%08x" % int(State,16)}')
                        else:
                              print(f'#Set Bank:{BankIndex} fail, Return:{"%d" % nReturn}, please check the Bank DIO type whether is input or output.')                       
                  else:
                        print(f'State:{State} is invalid parameter')
            else:
                  print(f'Bank Index:{BankIndex} is invalid parameter')
      else:
            print(f'line:{line} is invalid parameter')

def SdcGetDioState():

      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):
            
            print('Bank Index?( 0:input / 1:output )')
            BankIndex = input()

            if (re.search('^[0-9]+$', BankIndex)):

                  State = c_int()

                  nReturn = sdc.sdc_dio_get_bank_state(int(line), int(BankIndex), byref(State))

                  if(nReturn == STATUS_SUCCESS):
                        
                        print(f'#Get Bank:{BankIndex} State:0x{"%08x" % State.value}')
                  else:
                        print(f'#Get Bank:{BankIndex} fail, Return:{"%d" % nReturn}')
            else:
                  print(f'Bank Index:{BankIndex} is invalid parameter') 
      else:
            print(f'line:{line} is invalid parameter')

def SdcSetInpEvt():
    
      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):

            print('Bank Index?(0:input / 1:output):')
            BankIndex = input()
            
            if (re.search('^[0-9]+$', BankIndex)): 

                print('Rising edge event?(4 byte hex value, e.g. 000000FF):')
                Rising = input()

                print('Falling edge event?(4 byte hex value, e.g. 000000FF):')
                Falling = input()

                if((len(Rising) == 8) and (re.search('^[0-9a-fA-F]+$', Rising))):
                        
                    if((len(Falling) == 8) and (re.search('^[0-9a-fA-F]+$', Falling))):
                        
                        nReturn = sdc.sdc_dio_set_bank_input_event_ctrl(int(line), int(BankIndex), int(Rising,16), int(Falling,16))
                        
                        if(nReturn == STATUS_SUCCESS):
                            print('###################################################')
                            print(f'#Set Bank:{BankIndex} Rising edge event:0x{"%08x" % int(Rising,16)}')
                            print(f'#Set Bank:{BankIndex} Falling edge event:0x{"%08x" % int(Falling,16)}')
                            print('###################################################')
                        else:
                            print(f'#Set Bank:{BankIndex} input event control fail, Return:{"%d" % nReturn}, please check the Bank DIO type whether is input or output.')
                    else:
                        print(f'Falling edge event:{Falling} is invalid parameter')
                else:
                    print(f'Rising edge event:{Rising} is invalid parameter')
            else:
                  print(f'Bank Index:{BankIndex} is invalid parameter')
      else:
            print(f'line:{line} is invalid parameter')

def SdcGetInpEvt():

      print('line?(positive integer):')
      line = input()

      if re.search('^[0-9]+$', line):
            
            print('Bank Index?( 0:input / 1:output )')
            BankIndex = input()

            if (re.search('^[0-9]+$', BankIndex)):

                  rising = c_int()
                  falling = c_int()

                  nReturn = sdc.sdc_dio_get_bank_input_event_ctrl(int(line), int(BankIndex), byref(rising), byref(falling))

                  if(nReturn == STATUS_SUCCESS):
                        print('###################################################')
                        print(f'#Get Bank:{BankIndex} Rising edge event:0x{"%08x" % rising.value}')
                        print(f'#Get Bank:{BankIndex} Falling edge event:0x{"%08x" % falling.value}')
                        print('###################################################')
                  else:
                        print(f'#Get Bank:{BankIndex} input event control fail, Return:{"%d" % nReturn}')
            else:
                  print(f'Bank Index:{BankIndex} is invalid parameter') 
      else:
            print(f'line:{line} is invalid parameter')

def SdcRegInpEvtCallback():
      
    print('line?(positive integer):')
    line = input()

    if re.search('^[0-9]+$', line):

      InputEventCallbackType = CFUNCTYPE(c_void_p, c_int, c_int, c_int, c_int)

      SdcInpEvtCallback =  InputEventCallbackType(SdcInputEventCallbackFunc)

      nReturn = sdc.sdc_dio_register_event_callback(int(line),SdcInpEvtCallback)
      
      if(nReturn == 0):
            print('###################################################')
            print('sdc_dio_register_event_callback ok')
            print('###################################################')
      else:
            print('###################################################')
            print(f'sdc_dio_register_event_callback fail, Return:{"%d" % nReturn}')
            print('###################################################')
    else:
            print(f'line:{line} is invalid parameter')

def SdcUnRegInpEvtCallback():
      
    print('line?(positive integer):')
    line = input()

    if re.search('^[0-9]+$', line):

      nReturn = sdc.sdc_dio_unregister_event_callback(int(line))
      
      if(nReturn == 0):
            print('###################################################')
            print('sdc_dio_unregister_event_callback ok')
            print('###################################################')
      else:
            print('###################################################')
            print(f'sdc_dio_unregister_event_callback fail. eturn:{"%d" % nReturn}')
            print('###################################################')
    else:
            print(f'line:{line} is invalid parameter')

def main():
    
    SdcDioTestList()

    while True: 

        print()
        print('CMD ?')
        key = click.getchar()
      

        if(key == '1'):

            SdcDioOpen()
      
        elif(key == '2'):
      
            SdcDioClose()
        
        elif(key == 'A' or key == 'a'):
      
            SdcDumpDioInfo()
        
        elif(key == 'B' or key == 'b'):
      
            SdcSetDioState()
        
        elif(key == 'C' or key == 'c'):
      
            SdcGetDioState()
        
        elif(key == 'D'or key == 'd'):
      
            SdcSetInpEvt()
        
        elif(key == 'E'or key == 'e'):
      
            SdcGetInpEvt()
        
        elif(key == 'F'or key == 'f'):
      
            SdcRegInpEvtCallback()
        
        elif(key == 'G'or key == 'g'):
      
            SdcUnRegInpEvtCallback()
      
        elif(key == '?'):
      
            SdcDioTestList()

        elif(key == 'Q'or  key == 'q'):
      
            quit()

        time.sleep(0.2)

if __name__ == '__main__':
      main()