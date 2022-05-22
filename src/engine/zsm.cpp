/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "zsm.h"
#include "../ta-log.h"
#include "../utfutils.h"
#include "song.h"

ZSM::ZSM() {
  w = NULL;
  init();
}

ZSM::~ZSM() {
}

void ZSM::init(unsigned int rate) {
  if (w != NULL) {
	delete w;
  }
  w = new SafeWriter;
  w->init();
  // write default ZSM data header
  w->write("zm",2); // magic header
  w->writeC(ZSM_VERSION);
  // no loop offset
  w->writeS(0);
  w->writeC(0);
  // no PCM
  w->writeS(0x00);
  w->writeC(0x00);
  // FM channel mask
  w->writeC(0x00);
  // PSG channel mask
  w->writeS(0x00);
  w->writeS((unsigned short)rate);
  // 2 reserved bytes (set to zero)
  w->writeS(0x00);
  tickRate = rate;
  loopOffset=-1;
  numWrites=0;
  memset(&ymState,-1,sizeof(ymState));
  memset(&psgState,-1,sizeof(psgState));
  ticks=0;
}

int ZSM::getoffset() {
  return w->tell();
}

void ZSM::writeYM(unsigned char a, unsigned char v) {
  numWrites++;
  ymwrites.push_back(DivRegWrite(a,v));
  // update YM shadow even though we're not yet de-duping YM writes....
  if (a==0x19 && v>=0x80) a=0x1a; // AMD/PSD use same reg addr. store PMD as 0x1a
  if (a!=0x08) {
	ymState[ym_NEW][a] = v;
  }
}

void ZSM::writePSG(unsigned char a, unsigned char v) {
  if (a  >= 64) {
	  logD ("ZSM: ignoring VERA PSG write a=%02x v=%02x");
	  return;
  }
  if(psgState[psg_PREV][a] == v) {
	if (psgState[psg_NEW][a] != v)
	  numWrites--;
  } else {
    if (psgState[psg_PREV][a] == psgState[psg_NEW][a])
      numWrites++;
  }
  psgState[psg_NEW][a] = v;
}

void ZSM::writePCM(unsigned char a, unsigned char v) {
  // ZSM standard for PCM playback has not been established yet.
}

void ZSM::tick(int numticks) {
  if (numWrites > 0) {
    flushTicks();
    flushWrites();
  }
  ticks += numticks;
}

void ZSM::setLoopPoint() {
  tick(0); // flush any ticks+writes
  flushTicks(); // flush ticks if no writes
  logI("ZSM: loop at file offset %d bytes",w->tell());
  loopOffset=w->tell();
  w->seek(0x03,SEEK_SET);
  w->writeS((short)(loopOffset&0xffff));
  w->writeC((short)((loopOffset>>16)&0xff));
  w->seek(loopOffset,SEEK_SET);
  memset(&psgState,-1,sizeof(psgState));
}

SafeWriter* ZSM::finish() {
  tick(0);
  flushTicks();
  w->writeC(ZSM_EOF);
  // todo: put PCM offset/data writes here once defined in ZSM standard.
  // todo: set channel-use bitmasks in header
  //       -- really need to do it because player STOP does not
  //       -- mute voices otherwise!
  return w;
}

void ZSM::flushWrites() {
  if (numWrites==0) return;
  for (int i=0;i<64;i++) {
	if (psgState[psg_NEW][i] == psgState[psg_PREV][i]) continue;
	psgState[psg_PREV][i]=psgState[psg_NEW][i];
	w->writeC(i);
	w->writeC(psgState[psg_NEW][i]);
  }
  int n=0;
  for (DivRegWrite& write: ymwrites) {
	if (n%ZSM_YM_MAX_WRITES == 0) {
      if(ymwrites.size()-n > ZSM_YM_MAX_WRITES) {
		w->writeC(ZSM_YM_CMD+ZSM_YM_MAX_WRITES);
		logD("ZSM: YM-write: %d (%02x) [max]",ZSM_YM_MAX_WRITES,ZSM_YM_MAX_WRITES+ZSM_YM_CMD);
	  } else {
		w->writeC(ZSM_YM_CMD+ymwrites.size()-n);
		logD("ZSM: YM-write: %d (%02x)",ymwrites.size()-n,ZSM_YM_CMD+ymwrites.size()-n);
	  }
    }
    n++;
    w->writeC(write.addr);
    w->writeC(write.val);
  }
  ymwrites.clear();
  ticks=0;
  numWrites=0;
}

void ZSM::flushTicks() {
  while (ticks > ZSM_DELAY_MAX) {
	logD("ZSM: write delay %d (max)",ZSM_DELAY_MAX);
	w->writeC(ZSM_DELAY_CMD+ZSM_DELAY_MAX);
	ticks -= ZSM_DELAY_MAX;
  }
  if (ticks>0) {
	logD("ZSM: write delay %d",ticks);
	w->writeC(ZSM_DELAY_CMD+ticks);
  }
  ticks=0;
}
