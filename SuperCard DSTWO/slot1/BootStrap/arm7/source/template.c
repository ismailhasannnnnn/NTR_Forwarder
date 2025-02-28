/*---------------------------------------------------------------------------------

default ARM7 core

Copyright (C) 2005 - 2010
	Michael Noland (joat)
	Jason Rogers (dovoto)
	Dave Murphy (WinterMute)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
	must not claim that you wrote the original software. If you use
	this software in a product, an acknowledgment in the product
	documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
	must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
	distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>

//---------------------------------------------------------------------------------
void VcountHandler() {
//---------------------------------------------------------------------------------
	inputGetAndSend();
}

//---------------------------------------------------------------------------------
int main(void) {
//---------------------------------------------------------------------------------
	// SCFG_EXT
	// 0x92A00000 : NTR
	// 0x93FFFF07 : TWL
	// 0x93FF0F07 : max accessible in NTR mode
	if(REG_SCFG_EXT == 0x92A00000) {
		REG_SCFG_EXT |= 0x830F0100; // NAND ACCESS
		// SCFG_CLK
		// 0x0180 : NTR
		// 0x0187 : TWL
		// 
		if (!REG_SCFG_CLK == 0x0187)
			{REG_SCFG_CLK |= 1;}
	}	

	irqInit();
	fifoInit();

	// read User Settings from firmware
	readUserSettings();

	// Start the RTC tracking IRQ
	initClockIRQ();

	SetYtrigger(80);

	installSystemFIFO();
	
	irqSet(IRQ_VCOUNT, VcountHandler);

	irqEnable( IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);
	
	if(fifoCheckValue32(FIFO_USER_01))
	{
		if (REG_SCFG_CLK = 0x0181)
			{REG_SCFG_CLK = 0x0180;}
		REG_SCFG_EXT = 0x12A00000;
	} else { /* Do nothing */ }

	// Keep the ARM7 mostly idle
	while (1) {
	swiWaitForVBlank(); }
}


