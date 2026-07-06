I have analyzed the three core dumps you provided using the official Vita SDK tool (vita-
parse-core ).

In all three cases the result is identical, which means that the game is systematically crashing for the same instruction when  
 boot to real hardware:

• Exception: Data abort exception (0x30004) - Attempt to access invalid memory (segment violation).  
 • Collapsing Thread: The main thread of the game POPC00001 (ID: 0x40010003 ).  
 • PC (Program Counter): 0x9a0c58e0  
 • LR (Link Register): 0x9909daf5  
 • Relevant registers: R0 (0x0) and R3 (0x0). Since it is zero, and a Data abort is presented, it is almost certain that the code is trying to
read or write using a null pointer (Null pointer dereference) in the current statement.

### What does this mean?

The addresses 0x9A... and 0x99... do not belong to the Vita's base native executable ( so_loader ), which is usually hosted on the  
 range 0x81... . These high addresses correspond to the memory area where kubridge loads the Android dynamic libraries (  
 libcocos2d.so or libgame_logic.so ).

Since the console crashes so quickly with a Data Abort, there are two possible usual suspects in Cocos2d-x ports:

1. Missing libshacccg.suprx or failed graphical initialization: If your console does not have the shader compiler, or something in the  
   GL initialization fails silently, the game assumes that the video context (pointer) was created, and attempts to access it, throwing  
   null pointer.
2. NPOT (Non-Power of 2) Textures: As we mentioned in the INSTALL_HARDWARE.md file, the actual PS Vita hardware often generates  
   a "Data Abort" if the game sends textures of non-power-of-2 sizes to the GPU without handling them correctly, something that Vita3K forgives
   and emulates without problems.

To find out the exact line of code that is failing:
If you manage to capture the console log over the network at the time of launching the game (using plugins like PrincessLog or network tools from
VitaShell), you will be able to see the "base address" where the .so was loaded. Subtracting that base from 0x9a0c58e0 , you will get the exact offset. with that  
 offset, we can do an objdump to libgame_logic.so and find out the precise function that is breaking out.