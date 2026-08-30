typedef unsigned char   undefined;

typedef unsigned char    bool;
typedef unsigned char    byte;
typedef unsigned int    uint;
typedef unsigned char    undefined1;
typedef unsigned short    undefined2;
typedef unsigned int    undefined4;
typedef unsigned short    ushort;


byte *DAT_0000004c;
undefined1 *DAT_0000004c;
byte *DAT_0000016c;
byte *DAT_000006a8;
char *DAT_0000069c;
char *DAT_00000488;
char *DAT_000006a8;
short *DAT_0000057c;
int DAT_00000518;
int DAT_00000514;
byte *DAT_00000700;
undefined1 *DAT_00000708;
char *DAT_00001318;
int DAT_00002058;
int DAT_00002090;
undefined4 *DAT_000027e8;
undefined4 DAT_000027ec;
undefined1 DAT_fffffe92;
undefined DAT_ffffff9c;
undefined DAT_ffffffb0;
undefined1 DAT_fffffe72;
undefined DAT_ffffff90;
undefined DAT_ffffff94;
undefined DAT_ffffff98;
short DAT_0000479a;
byte *DAT_00004c30;
short DAT_00004b62;
undefined DAT_00004c38;
int DAT_00004c3c;
undefined *PTR_DAT_00004c40;
undefined1 *DAT_00004ea0;
undefined1 *DAT_00004eac;
uint *DAT_00004eb0;
uint DAT_00004ea4;
int *DAT_00004ea8;

int FUN_00000038(void)

{
  byte bVar1;
  
  bVar1 = *DAT_0000004c;
  *DAT_0000004c = bVar1 | 0x80;
  return (bVar1 == 0) - 1;
}



undefined4 FUN_00000042(undefined4 param_1)

{
  *DAT_0000004c = 0;
  return param_1;
}



bool FUN_000000f0(void)

{
  return (*DAT_0000016c & 1) == 0;
}



bool FUN_000000fa(void)

{
  return (*DAT_0000016c & 2) == 0;
}



int FUN_000001f8(uint param_1)

{
  return (param_1 & 0xff) * 4 + 0x208;
}



uint FUN_000002ac(uint param_1)

{
  bool bVar1;
  
  bVar1 = FUN_000000f0();
  if (!bVar1) {
    param_1 = param_1 | 0x30;
  }
  bVar1 = FUN_000000fa();
  if (!bVar1) {
    param_1 = param_1 | 0xffffffc0;
  }
  return param_1;
}



void FUN_0000042a(void)

{
  *DAT_000006a8 = *DAT_000006a8 | 2;
  return;
}



uint FUN_00000464(void)

{
  char *pcVar1;
  uint uVar2;
  uint uVar3;
  
  pcVar1 = DAT_00000488;
  uVar2 = (uint)*DAT_0000069c;
  if ((uVar2 & 0x10) != 0) {
    uVar2 = 3;
    uVar3 = (int)*DAT_00000488 + 1;
    if (2 < uVar3) {
      uVar3 = 0;
      uVar2 = (int)*DAT_000006a8 | 1;
      *DAT_000006a8 = (char)uVar2;
    }
    *pcVar1 = (char)uVar3;
  }
  return uVar2;
}



int FUN_0000048c(undefined4 *param_1,int *param_2)

{
  short sVar1;
  uint uVar2;
  uint uVar3;
  char *pcVar4;
  int iVar5;
  int iVar6;
  int iVar7;
  
  iVar7 = 0;
  iVar5 = *param_2;
  uVar2 = (uint)*(char *)*param_1;
  pcVar4 = (char *)*param_1 + 1;
  sVar1 = *DAT_0000057c;
  uVar3 = uVar2 & 0xf;
  if ((uVar2 != 0xfffffff0) && (uVar3 == 0)) {
    uVar3 = 1;
  }
  iVar6 = 0xf;
  do {
    if (uVar3 == 0) {
      pcVar4 = pcVar4 + -1;
      uVar2 = 0xffffffff;
      *pcVar4 = -1;
    }
    else {
      uVar2 = (uint)*pcVar4;
      uVar3 = uVar3 - 1;
      if (uVar2 != 0xffffffff) {
        iVar7 = iVar7 + 1;
      }
    }
    pcVar4 = (char *)(**(code **)((uint)*(byte *)((uVar2 & 0xf) +
                                                 *(int *)(((uVar2 & 0xf0) >> 2) + DAT_00000518)) +
                                 DAT_00000514))(pcVar4,iVar5,(int)sVar1);
    iVar5 = iVar5 + sVar1;
    iVar6 = iVar6 + -1;
  } while (iVar6 != 0);
  *param_1 = pcVar4;
  *param_2 = iVar5;
  return iVar7;
}



undefined4 FUN_000006b4(undefined1 *param_1,uint param_2)

{
  char cVar1;
  byte *pbVar2;
  
  pbVar2 = DAT_00000700;
  do {
  } while ((*DAT_00000700 & 1) != 0);
  cVar1 = param_1[2];
  *DAT_00000700 = 1;
  if (*(code **)(cVar1 + 0x6f0) != (code *)0x0) {
    (**(code **)(cVar1 + 0x6f0))();
  }
  *DAT_00000708 = *param_1;
  if ((param_2 & 0xff) != 0) {
    do {
    } while ((*pbVar2 & 1) != 0);
  }
  return 0;
}



void FUN_00000ea4(undefined4 param_1,int param_2)

{
  *(undefined2 *)(param_2 + 2) = 0xffff;
  *(undefined2 *)(param_2 + 4) = 0xffff;
  *(undefined2 *)(param_2 + 6) = 0xffff;
  return;
}



void FUN_00000eae(ushort param_1,int param_2)

{
  *(ushort *)(param_2 + 4) = ~(*(ushort *)(param_2 + 2) & ~param_1);
  *(ushort *)(param_2 + 6) = ~(~*(ushort *)(param_2 + 2) & param_1);
  *(ushort *)(param_2 + 2) = param_1;
  return;
}



int FUN_000012fa(void)

{
  int iVar1;
  
  while (*DAT_00001318 < 0) {
    iVar1 = 0x30;
    do {
      iVar1 = iVar1 + -1;
    } while (iVar1 != 0);
  }
  return (int)*DAT_00001318;
}



void FUN_0000168e(undefined1 param_1)

{
  int unaff_r8;
  
  do {
  } while ((*(byte *)(unaff_r8 + 99) & 1) != 0);
  *(byte *)(unaff_r8 + 99) = 1;
  *(undefined1 *)(unaff_r8 + 0x1f) = param_1;
  do {
  } while ((*(byte *)(unaff_r8 + 99) & 1) != 0);
  return;
}



void FUN_000016ac(char *param_1)

{
  int iVar1;
  
  if (param_1 != (char *)0x0) {
    while (iVar1 = 0x7f, *param_1 != '\0') {
      do {
        iVar1 = iVar1 + -1;
      } while (iVar1 != 0);
    }
  }
  return;
}



void FUN_0000200e(int param_1,int param_2,char param_3,char param_4)

{
  int iVar1;
  int iVar2;
  char unaff_r8;
  char unaff_r9;
  char *unaff_r10;
  char *pcVar3;
  char unaff_r11;
  
  iVar2 = param_1;
  pcVar3 = unaff_r10;
  do {
    *pcVar3 = -2;
    pcVar3[1] = param_3;
    pcVar3[2] = param_4;
    iVar2 = iVar2 + -1;
    pcVar3 = pcVar3 + 4;
    iVar1 = param_2;
  } while (iVar2 != 0);
  do {
    do {
      *pcVar3 = (char)unaff_r10 - unaff_r11;
      pcVar3[1] = unaff_r8;
      pcVar3[2] = unaff_r9;
      iVar1 = iVar1 + -1;
      pcVar3 = pcVar3 + 4;
    } while (iVar1 != 0);
    param_1 = param_1 + -1;
    unaff_r10 = unaff_r10 + 4;
    iVar1 = param_2;
  } while (param_1 != 0);
  return;
}



uint FUN_00002046(int param_1,int param_2,undefined4 param_3,uint param_4)

{
  int in_r0;
  int iVar1;
  int iVar2;
  uint uVar3;
  undefined1 unaff_r8;
  undefined1 *unaff_r10;
  
  iVar1 = (int)*(char *)(DAT_00002058 + in_r0);
  uVar3 = param_4;
  while( true ) {
    iVar1 = iVar1 + -1;
    param_4 = param_4 << 1;
    iVar2 = param_2;
    if (iVar1 == 0) break;
    uVar3 = uVar3 | param_4;
  }
  do {
    do {
      *unaff_r10 = 0xfd;
      unaff_r10[1] = unaff_r8;
      unaff_r10[2] = (char)uVar3;
      iVar2 = iVar2 + -1;
      unaff_r10 = unaff_r10 + 4;
    } while (iVar2 != 0);
    param_1 = param_1 + -1;
    iVar2 = param_2;
  } while (param_1 != 0);
  return uVar3;
}



int FUN_0000207a(void)

{
  char cVar1;
  undefined1 in_r0;
  undefined1 in_r1;
  undefined1 *unaff_r10;
  int unaff_gbr;
  
  unaff_r10[1] = in_r0;
  *unaff_r10 = in_r1;
  cVar1 = *(char *)(DAT_00002090 + ((int)*(short *)(unaff_gbr + 0x15c) & 3U));
  unaff_r10[2] = cVar1;
  return (int)cVar1;
}



uint FUN_00002476(char *param_1,int param_2,short *param_3,ushort param_4)

{
  char cVar1;
  uint uVar2;
  uint uVar3;
  uint uVar4;
  int iVar5;
  int iVar6;
  
  do {
    iVar6 = 8;
    do {
      cVar1 = *param_1;
      param_1 = param_1 + 1;
      iVar5 = 4;
      uVar4 = (int)cVar1 << 0x18;
      do {
        uVar2 = uVar4 & 0x80000000;
        uVar3 = uVar4 & 0x40000000;
        uVar4 = uVar4 << 2;
        *param_3 = (ushort)(uVar2 != 0) * 0x100 + (param_4 & 0xff) * 0x101 + (ushort)(uVar3 != 0);
        iVar5 = iVar5 + -1;
        param_3 = param_3 + 1;
      } while (iVar5 != 0);
      iVar6 = iVar6 + -1;
    } while (iVar6 != 0);
    param_2 = param_2 + -1;
  } while (param_2 != 0);
  return uVar4;
}



int FUN_000024a6(byte *param_1,int param_2,undefined2 *param_3,uint param_4)

{
  byte bVar1;
  int iVar2;
  int iVar3;
  int iVar4;
  
  do {
    iVar4 = 8;
    do {
      iVar3 = 4;
      do {
        bVar1 = *param_1;
        param_1 = param_1 + 1;
        iVar2 = ((int)(char)bVar1 & 0xfU | (uint)(bVar1 >> 4) << 8) +
                ((param_4 & 0xff) << 8 | param_4 & 0xff);
        *param_3 = (short)iVar2;
        iVar3 = iVar3 + -1;
        param_3 = param_3 + 1;
      } while (iVar3 != 0);
      iVar4 = iVar4 + -1;
    } while (iVar4 != 0);
    param_2 = param_2 + -1;
  } while (param_2 != 0);
  return iVar2;
}



undefined4 * FUN_000027d4(void)

{
  undefined4 *puVar1;
  int iVar2;
  undefined4 uVar3;
  undefined4 *puVar4;
  
  puVar1 = &DAT_000027ec;
  iVar2 = 8;
  puVar4 = DAT_000027e8;
  do {
    uVar3 = *puVar1;
    puVar1 = puVar1 + 1;
    puVar4 = puVar4 + 1;
    iVar2 = iVar2 + -1;
    *puVar4 = uVar3;
  } while (iVar2 != 0);
  return puVar1;
}



undefined4 FUN_000037f4(void)

{
  DAT_fffffe92 = 0x11;
  return 0x11;
}



// WARNING: Globals starting with '_' overlap smaller symbols at the same address

undefined4 FUN_0000468a(undefined4 param_1,undefined4 param_2,undefined4 param_3)

{
  do {
  } while ((_DAT_ffffff9c & 3) == 1);
  do {
  } while ((_DAT_ffffff9c & 3) == 1);
  DAT_fffffe72 = 0;
  _DAT_ffffff90 = param_1;
  _DAT_ffffff94 = param_2;
  _DAT_ffffff98 = param_3;
  _DAT_ffffff9c = (int)DAT_0000479a;
  _DAT_ffffffb0 = 9;
  return 9;
}



// WARNING: Control flow encountered bad instruction data

void FUN_000048d4(void)

{
  switch(*DAT_00004c30 & 0x7c) {
  default:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 4:
  case 8:
  case 0xc:
  case 0x10:
  case 0x14:
  case 0x1c:
  case 0x20:
  case 0x24:
  case 0x28:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 0x18:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 0x2c:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 0x44:
  case 0x48:
  case 0x4c:
  case 0x50:
  case 0x54:
  case 0x5c:
  case 0x60:
  case 100:
  case 0x68:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 0x58:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  case 0x6c:
                    // WARNING: Bad instruction - Truncating control flow here
    halt_baddata();
  }
}



void FUN_00004af0(undefined4 param_1,int param_2,uint param_3,uint param_4)

{
  int iVar1;
  undefined4 *extraout_r3;
  undefined4 *extraout_r3_00;
  undefined4 *puVar2;
  int unaff_gbr;
  
  FUN_00004b42();
  extraout_r3[4] = 0;
  puVar2 = extraout_r3;
  if (param_3 < param_4) {
    *extraout_r3 = &DAT_00004c38;
    extraout_r3[1] = param_2 + param_3;
    extraout_r3[2] = param_4 - param_3;
    extraout_r3[3] = 1;
    *(undefined1 *)(unaff_gbr + 0xbd) = 8;
    iVar1 = (int)DAT_00004b62;
    extraout_r3[5] = 7;
    extraout_r3[4] = iVar1;
    FUN_00004b42();
    extraout_r3_00[4] = 0;
    puVar2 = extraout_r3_00;
    param_4 = param_3;
  }
  if (param_4 != 0) {
    iVar1 = (int)DAT_00004b62;
    *puVar2 = param_1;
    puVar2[1] = param_2;
    puVar2[2] = param_4;
    puVar2[3] = iVar1;
    puVar2[5] = 7;
    puVar2[4] = iVar1;
    *(undefined1 *)(unaff_gbr + 0xbd) = 8;
  }
  return;
}



int FUN_00004b42(void)

{
  byte bVar1;
  int iVar2;
  int unaff_gbr;
  
  bVar1 = *(byte *)(unaff_gbr + 0xbd);
  while ((iVar2 = 0x3c, (bVar1 & 4) == 0 &&
         (iVar2 = 0x30, ((uint)PTR_DAT_00004c40 & *(uint *)(DAT_00004c3c + 0x3c)) != 0))) {
    do {
      iVar2 = iVar2 + -1;
    } while (iVar2 != 0);
    bVar1 = *(byte *)(unaff_gbr + 0xbd);
  }
  return iVar2;
}



int FUN_00004dd0(void)

{
  char cVar1;
  undefined1 *puVar2;
  char *pcVar3;
  int iVar4;
  char *pcVar5;
  char *pcVar6;
  
  puVar2 = DAT_00004eac;
  pcVar5 = DAT_00004ea0 + 2;
  *DAT_00004eac = *DAT_00004ea0;
  iVar4 = 7;
  pcVar3 = puVar2 + 4;
  do {
    pcVar6 = pcVar3;
    cVar1 = *pcVar5;
    pcVar5 = pcVar5 + 2;
    *pcVar6 = cVar1;
    iVar4 = iVar4 + -1;
    pcVar3 = pcVar6 + 1;
  } while (iVar4 != 0);
  pcVar6 = pcVar6 + 2;
  iVar4 = 4;
  do {
    cVar1 = *pcVar5;
    pcVar5 = pcVar5 + 2;
    *pcVar6 = cVar1;
    pcVar6 = pcVar6 + 1;
    iVar4 = iVar4 + -1;
  } while (iVar4 != 0);
  iVar4 = 4;
  do {
    cVar1 = *pcVar5;
    pcVar5 = pcVar5 + 2;
    *pcVar6 = cVar1;
    pcVar6 = pcVar6 + 1;
    iVar4 = iVar4 + -1;
  } while (iVar4 != 0);
  return (int)cVar1;
}



void FUN_00004e0c(void)

{
  undefined1 uVar1;
  uint uVar2;
  int iVar3;
  undefined1 *puVar4;
  int iVar5;
  
  uVar2 = *DAT_00004eb0;
  if (uVar2 < DAT_00004ea4) {
    iVar5 = *DAT_00004ea8;
    iVar3 = 0x20;
    puVar4 = DAT_00004ea0;
    do {
      uVar1 = *puVar4;
      puVar4 = puVar4 + 2;
      *(undefined1 *)(iVar5 + uVar2) = uVar1;
      uVar2 = uVar2 + 1;
      iVar3 = iVar3 + -1;
    } while (iVar3 != 0);
  }
  *DAT_00004eb0 = uVar2;
  return;
}


