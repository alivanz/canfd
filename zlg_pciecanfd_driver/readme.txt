1. 查看内核
	uname -a
	应该输出类似情况，Linux zlg 5.15.0-125-generic #135~20.04.1-Ubuntu SMP Mon Oct 7 13:56:22 UTC 2024 x86_64 x86_64 x86_64 GNU/Linux

	修改make文件，将第一行改成如上的内核。 KDIR ?= /usr/src/linux-headers-5.15.0-125-generic
	
2. 编译
	make
	生成xpcfd.ko文件

3. 安装环境	
	sudo apt-get update
	sudo apt-get install can-utils
	sudo modprobe can-dev
	sudo modprobe sja1000
	sudo insmod xpcfd.ko

4. 检查系统是否识别设备
	lspci -n
	
	PCIeCANFD-400U		10ee:9a04
	PCIeCANFD-200U		10ee:9a02
	MINIPCIeCANFD		10ee:9a12
	M.2CANFD			10ee:9a22
	PCIeCANFD-800U		1feb:0108
	PCIeCANFD-1200U		1feb:0108

5. 检查驱动安装情况
	ls /sys/class/net/ | grep can

6. 设备操作
	设置电阻
	sudo echo 0xfff > /sys/class/net/can0/device/term_res
	或者 sudo vi /sys/class/net/can0/device/term_res
	将文件里面内容改成0xfff
	
	设置通道参数
	sudo ip link set can0 type can fd on bitrate 500000 dbitrate 2000000 sample-point 0.8 dsample-point 0.8
	
	启动
	sudo ip link set can0 up
	
	关闭通道
	sudo ip link set can0 down
	
	获取通道配置
	ip -details link show can0
	
	接收报文（单独开一个终端控制台）
	candump any				接收所有通道 -c
	candump can0				接收单一通道
	candump -e any,0:0,#FFFFFFFF 		接收错误帧和正常报文

	发送CAN报文
	cansend can0 123#11.22.33.44.55.66.77.88   			CAN0口发送ID为123的8字节CAN标准帧；
	cansend can0 00000123#11.22.33.44.55.66.77.88  		CAN0口发送ID为123的8字节CAN扩展帧
	cansend can0 123##2.00.11.22.33.44.55.66.77.88 		CAN0口发送ID为123的12字节CANFD标准帧(这里构造9字节，实际根据CANFD协议需要填充至12字节)
	cansend can0 123##3.00.11.22.33.44.55.66.77.88		CAN0口发送ID为123的12字节CANFD加速标准帧

	配置帮助
	sudo ip link set can0 up type can help
	
	设置自定义波特率，需要波特率计算器
	sudo ip link set can0 type can fd on tq 100 prop-seg 1 phase-seg1 13 phase-seg2 5 sjw 3 dtq 25 dprop-seg 1 dphase-seg1 13 dphase-seg2 5 dsjw 3
	
8. 卸载驱动
	sudo rmmod xpcfd
	
9. 接收问题
	socketcan的驱动，受限于系统socketcan的设置，驱动本身接收不会丢帧（可以通过ifconfig验证）。
	如遇到candump丢帧，而ifconfig不丢帧，可以通过下面两条命令修改内核关于socketcan的接收缓冲区大小。
	   
	设置接收缓冲区最大值（单位：字节，例如设为 25MB）
	 sudo sysctl -w net.core.rmem_max=26214400

	设置默认接收缓冲区大小（建议与 rmem_max 保持一致）
	 sudo sysctl -w net.core.rmem_default=26214400
	
