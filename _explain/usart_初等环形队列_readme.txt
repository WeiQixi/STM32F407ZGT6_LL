USART 初等环形队列 readme
========================

本文件是 USART 初等环形队列专题的 readme，只讲接收环形缓冲，不覆盖：
	USART 初始化		_explain/usart1_ll_init.html
	USB FS				_explain/usb_fs_ll_init.html
	FreeRTOS			_explain/freertos_ll_demo_readme.html
	工程总说明			FreeRTOS_LL_demo/readme.txt

网页版：_explain/usart_初等环形队列.html

一、当前硬件（已从 USART1 改到 USART6）
	MCU：STM32F407ZGT6。
	USART6_TX = PC6，AF8。
	USART6_RX = PC7，AF8，上拉。
	PCLK2 = 84MHz，115200 8N1。
	座子：原理图 P13 的 UART6-TTL，不要再插 P20。
	文件：Hardware/usart/usart6_ll.c、usart6_ll.h。
	中断名必须是 USART6_IRQHandler，向量表不认 USART1_IRQHandler。

	PA9/PA10 已空出。USB 仍然不要把 PA9 配成 VBUS。

二、这是什么队列
	128 字节数组 + head + tail。
	head：下一个空位，只由中断 RxPush 写。
	tail：下一个未读字节，只由 main 里 ReadByte 写。
	绕圈用 (下标 + 1) % 128。

	这是单生产者、单消费者、满了丢新字节、留一格区分空/满的初等写法。
	不是环形队列终极版。当前串口回显够用。

三、空和满
	空：现在的 head == 现在的 tail。没有未读数据。
	满：(head + 1) % 128 == 现在的 tail。再写会盖掉还没读走的。

	先算 next，不要先改 head。
	next == tail 则满：不写缓冲，head 保持原值，next 是局部变量，return 后消失。
	128 格最多存 127 字节，故意留一格。否则满和空都会变成 head == tail。

四、两个函数
	RxPush（中断里）
		next = (head + 1) % 128
		若 next == tail，丢这一字节并 return
		否则写入 buf[head]，再 head = next

	ReadByte（main 里）
		data 为空指针则返回 0
		head == tail 则空，返回 0
		否则取出 buf[tail]，tail 前进一格，返回 1

	main 回显：
		if (USART6_LL_ReadByte(&ch)) USART6_LL_SendByte(ch);

五、为什么能收超过 128 字节
	128 是同时最多暂存 127 字节，不是累计上限。
	中断推进 head，main 取走后 tail 跟上，格子会反复使用。
	边收边回显时，缓冲里通常只有几字节，所以一次贴几百字节也能原样回来。
	只有来得比取走快、未读顶到 127 时，RxPush 才丢新字节。

六、中断和 main
	中断可以打断 main，但 ISR 只读 DR、入队，很快返回。
	main 从打断处继续，再 ReadByte、等 TXE 回发。
	不要在 ISR 里 while (!TXE) 发送，也不要在 ISR 里 printf。
	NVIC 优先级 5，对齐 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。
	当前 ISR 不调 FreeRTOS API。ORE 先读 DR 再 return，避免中断卡死。

七、不是终极版
	现在：% 绕圈、留一格、丢新字节、逐字节。
	以后按需再换：2 的幂用 & 代替 %、DMA+IDLE、FreeRTOS 队列。
	DMA 尚未做。调度器尚未启动，回显仍在 main 循环。

八、上板
	P13 UART6，115200 8N1，关掉本地回显。
	复位后应先看到 Hello, World!，再键入原样回来。
