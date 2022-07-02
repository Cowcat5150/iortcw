/*
 * Copyright (C) 2005 Mark Olsen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <exec/exec.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#define USE_INLINE_STDARG
#include <proto/ahi.h>

#include "../qcommon/q_shared.h"
#include "../client/snd_local.h"

struct AHIdata *ad;

struct AHIChannelInfo
{
	struct AHIEffChannelInfo aeci;
	ULONG offset;
};

struct AHIdata
{
	struct MsgPort *msgport;
	struct AHIRequest *ahireq;
	int ahiopen;
	struct AHIAudioCtrl *audioctrl;
	void *samplebuffer;
	struct Hook EffectHook;
	struct AHIChannelInfo aci;
	unsigned int readpos;
};

ULONG EffectFunc()
{
	struct Hook *hook = (struct Hook *)REG_A0;
	struct AHIEffChannelInfo *aeci = (struct AHIEffChannelInfo *)REG_A1;

	struct AHIdata *ad;

	ad = hook->h_Data;

	ad->readpos = aeci->ahieci_Offset[0];

	return 0;
}

static struct EmulLibEntry EffectFunc_Gate =
{
	TRAP_LIB, 0, (void (*)(void))EffectFunc
};


qboolean SNDDMA_Init(void)
{
	ULONG channels;
	ULONG speed;
	ULONG bits;
	unsigned int buffersize;

	ULONG r;

	struct Library *AHIBase;

	struct AHISampleInfo sample;

	cvar_t *sndbits;
	cvar_t *sndspeed;
	cvar_t *sndchannels;

	char modename[64];

	if (ad)
		return qfalse;

	sndbits = Cvar_Get("sndbits", "16", CVAR_ARCHIVE);
	sndspeed = Cvar_Get("sndspeed", "0", CVAR_ARCHIVE);
	sndchannels = Cvar_Get("sndchannels", "2", CVAR_ARCHIVE);

	speed = sndspeed->integer;

	if (speed == 0)
		speed = 22050;

	ad = AllocVec(sizeof(*ad), MEMF_ANY);

	if (ad)
	{
		ad->msgport = CreateMsgPort();

		if (ad->msgport)
		{
			ad->ahireq = (struct AHIRequest *)CreateIORequest(ad->msgport, sizeof(struct AHIRequest));

			if (ad->ahireq)
			{
				ad->ahiopen = !OpenDevice("ahi.device", AHI_NO_UNIT, (struct IORequest *)ad->ahireq, 0);

				if (ad->ahiopen)
				{
					AHIBase = (struct Library *)ad->ahireq->ahir_Std.io_Device;

					ad->audioctrl = AHI_AllocAudio(AHIA_AudioID, AHI_DEFAULT_ID,
					                               AHIA_MixFreq, speed,
					                               AHIA_Channels, 1,
					                               AHIA_Sounds, 1,
					                               TAG_END);

					if (ad->audioctrl)
					{
						AHI_GetAudioAttrs(AHI_INVALID_ID, ad->audioctrl,
						                  AHIDB_BufferLen, sizeof(modename),
						                  AHIDB_Name, (ULONG)modename,
						                  AHIDB_MaxChannels, (ULONG)&channels,
						                  AHIDB_Bits, (ULONG)&bits,
						                  TAG_END);

						AHI_ControlAudio(ad->audioctrl,
						                 AHIC_MixFreq_Query, (ULONG)&speed,
						                 TAG_END);

						if (bits == 8 || bits == 16)
						{
							if ( strncmp(modename, "EMU10kx:", 8) == 0
								|| strncmp(modename, "Unit 0:", 7) == 0
								|| strncmp(modename, "Unit 1:", 7) == 0
								|| strncmp(modename, "Unit 2:", 7) == 0
								|| strncmp(modename, "Unit 3:", 7) == 0 )
							{
								buffersize = 16384;
							}

							else
							{
								buffersize = 4096;
							}

							if (channels > 2)
								channels = 2;

							dma.speed = speed;
							dma.samplebits = bits;
							dma.channels = channels;
							dma.samples = buffersize*(speed/11025);
							dma.fullsamples = dma.samples / dma.channels; // new !! Cowcat
							dma.submission_chunk = 1;

							ad->samplebuffer = AllocVec(buffersize*(speed/11025)*(bits/8)*channels, MEMF_ANY|MEMF_CLEAR);

							if (ad->samplebuffer)
							{
								dma.buffer = ad->samplebuffer;

								if (channels == 1)
								{
									if (bits == 8)
										sample.ahisi_Type = AHIST_M8S;

									else
										sample.ahisi_Type = AHIST_M16S;
								}

								else
								{
									if (bits == 8)
										sample.ahisi_Type = AHIST_S8S;

									else
										sample.ahisi_Type = AHIST_S16S;
								}

								sample.ahisi_Address = ad->samplebuffer;
								sample.ahisi_Length = (buffersize*(speed/11025)*(bits/8))/AHI_SampleFrameSize(sample.ahisi_Type);

								r = AHI_LoadSound(0, AHIST_DYNAMICSAMPLE, &sample, ad->audioctrl);

								if (r == 0)
								{
									r = AHI_ControlAudio(ad->audioctrl, AHIC_Play, TRUE, TAG_END);

									if (r == 0)
									{
										AHI_Play(ad->audioctrl,
										         AHIP_BeginChannel, 0,
										         AHIP_Freq, speed,
										         AHIP_Vol, 0x10000,
										         AHIP_Pan, 0x8000,
										         AHIP_Sound, 0,
										         AHIP_EndChannel, NULL,
										         TAG_END);

										ad->aci.aeci.ahie_Effect = AHIET_CHANNELINFO;
										ad->aci.aeci.ahieci_Func = &ad->EffectHook;
										ad->aci.aeci.ahieci_Channels = 1;

										ad->EffectHook.h_Entry = (void *)&EffectFunc_Gate;
										ad->EffectHook.h_Data = ad;

										AHI_SetEffect(&ad->aci, ad->audioctrl);

										Com_Printf("Using AHI mode \"%s\" for audio output\n", modename);
										Com_Printf("Channels: %d bits: %d frequency: %d\n", channels, bits, speed);

										return 1;
									}
								}
							}

							FreeVec(ad->samplebuffer);
						}

						AHI_FreeAudio(ad->audioctrl);
					}

					else
						Com_Printf("Failed to allocate AHI audio\n");

					CloseDevice((struct IORequest *)ad->ahireq);
				}

				DeleteIORequest((struct IORequest *)ad->ahireq);
			}

			DeleteMsgPort(ad->msgport);
		}

		FreeVec(ad);
	}

	return qtrue;
}

int SNDDMA_GetDMAPos(void)
{
	return ad->readpos*dma.channels;
}

void SNDDMA_Shutdown(void)
{
	struct Library *AHIBase;

	if (ad == 0)
		return;

	AHIBase = (struct Library *)ad->ahireq->ahir_Std.io_Device;

	ad->aci.aeci.ahie_Effect = AHIET_CHANNELINFO|AHIET_CANCEL;
	AHI_SetEffect(&ad->aci.aeci, ad->audioctrl);
	AHI_ControlAudio(ad->audioctrl, AHIC_Play, FALSE, TAG_END);
	AHI_FreeAudio(ad->audioctrl);
	FreeVec(ad->samplebuffer);
	CloseDevice((struct IORequest *)ad->ahireq);
	DeleteIORequest((struct IORequest *)ad->ahireq);
	DeleteMsgPort(ad->msgport);
	FreeVec(ad);

	ad = 0;
}

void SNDDMA_Submit(void)
{
}

void SNDDMA_BeginPainting (void)
{
}
