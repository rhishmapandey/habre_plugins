#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ladspa.h>
#include "utils.h"
#include "slib.h"
/*****************************************************************************/

/* The port numbers for the plugin: */

#define AMP_INPUT1  0
#define AMP_OUTPUT1 1
#define AMP_INPUT2  2
#define AMP_OUTPUT2 3
#define AMP_CONTROL 4
#define AMP_THL 5
#define AMP_THH 6
#define AMP_DETAIL 7
#define AMP_DECAY 8
#define AMP_ECHOSCALE 9
#define AMP_ROOMSIZE 10
#define AMP_MIX 11

#define AMP_PORTS_COUNT 12
/*****************************************************************************/

#define AMP_MAX_REVERB_DETAIL 100



/* The structure used to hold port connection information and state
   (actually gain controls require no further state). */
static unsigned int rate;
static unsigned long saminc1 = 0;
static unsigned long saminc2 = 0;

static float* samplebuffer1;
static float* samplebuffer2;
static float* reverbimpulses;

static int prevfilled1 = 0;
static int prevfilled2 = 0;
static unsigned long impulsesampleseperation = 0;

#define FCOUNT 25

#define TANKFOLDS 30

#define MAX_ROOM_SIZE 50


static float ps1[FCOUNT];
static float ps2[FCOUNT];
float* facs;
SLIB_SAMPLES magResponse;

float prev_freq1 = -9999;
float prev_freq2 = -9999;
float prev_decay = -9999;
float prev_echoscale = -9999;
float prev_roomsize = -9999;
float prev_mix = -9999;
float prev_detail = -9999;

typedef struct {

  /* Ports:
     ------ */

  LADSPA_Data * m_pfControlValue;
  LADSPA_Data * m_pfTHLFreq;
  LADSPA_Data * m_pfTHHFreq;
  LADSPA_Data * m_pfDetail;
  LADSPA_Data * m_pfDecay;
  LADSPA_Data * m_pfEchoScale;
  LADSPA_Data * m_pfRoomSize;
  LADSPA_Data * m_pfMix;
  LADSPA_Data * m_pfInputBuffer1;
  LADSPA_Data * m_pfOutputBuffer1;
  LADSPA_Data * m_pfInputBuffer2;  /* (Not used for mono) */
  LADSPA_Data * m_pfOutputBuffer2; /* (Not used for mono) */

} Amplifier;


/*****************************************************************************/

/* Construct a new plugin instance. */
static LADSPA_Handle
instantiateAmplifier(const LADSPA_Descriptor * Descriptor,
		     unsigned long               SampleRate){
  rate  = SampleRate;
  facs = (float*)malloc(sizeof(float)*FCOUNT);
  magResponse.asamples = (float*)malloc(sizeof(float)*SampleRate);
  magResponse.length = SampleRate;
  for (int i = 0; i < FCOUNT; i++) {
    ps1[0] = 0.0;
    ps2[0] = 0.0;
  }
  samplebuffer1 = (float*)malloc(sizeof(float)*rate*TANKFOLDS);
  samplebuffer2 = (float*)malloc(sizeof(float)*rate*TANKFOLDS);
  for (int i = 0; i < rate*TANKFOLDS; i++) {
    samplebuffer1[i] = 0.0f;
    samplebuffer2[i] = 0.0f;
  }
  reverbimpulses = (float*)malloc(sizeof(float)*AMP_MAX_REVERB_DETAIL);
  return malloc(  sizeof(Amplifier));
}

/*****************************************************************************/

/* Connect a port to a data location. */
static void
connectPortToAmplifier(LADSPA_Handle Instance,
		       unsigned long Port,
		       LADSPA_Data * DataLocation) {

  Amplifier * psAmplifier;

  psAmplifier = (Amplifier *)Instance;
  switch (Port) {
  case AMP_CONTROL:
    psAmplifier->m_pfControlValue = DataLocation;
    break;
  case AMP_THL:
    psAmplifier->m_pfTHLFreq = DataLocation;
    break;
  case AMP_THH:
    psAmplifier->m_pfTHHFreq = DataLocation;
    break;
  case AMP_DETAIL:
    psAmplifier->m_pfDetail = DataLocation;
    break;
  case AMP_DECAY:
    psAmplifier->m_pfDecay = DataLocation;
    break;
  case AMP_ECHOSCALE:
    psAmplifier->m_pfEchoScale = DataLocation;
    break;
  case AMP_ROOMSIZE:
    psAmplifier->m_pfRoomSize = DataLocation;
    break;
  case AMP_MIX:
    psAmplifier->m_pfMix = DataLocation;
    break;
  case AMP_INPUT1:
    psAmplifier->m_pfInputBuffer1 = DataLocation;
    break;
  case AMP_OUTPUT1:
    psAmplifier->m_pfOutputBuffer1 = DataLocation;
    break;
  case AMP_INPUT2:
    /* (This should only happen for stereo.) */
    psAmplifier->m_pfInputBuffer2 = DataLocation;
    break;
  case AMP_OUTPUT2:
    /* (This should only happen for stereo.) */
    psAmplifier->m_pfOutputBuffer2 = DataLocation;
    break;
  }
}

/***************revfac += reverbimpulses[k]*samplebuffer1[rate*TANKFOLDS-reflectiontraceindex];**************************************************************/

static void
runStereoEffect(LADSPA_Handle Instance,
		   unsigned long SampleCount) {

  LADSPA_Data * pfInput;
  LADSPA_Data * pfOutput;
  LADSPA_Data fGain;
  LADSPA_Data fTHL;
  LADSPA_Data fTHH;
  unsigned long fDetail;
  LADSPA_Data fDecay;
  LADSPA_Data fEchoScale;
  LADSPA_Data fRoomSize;
  LADSPA_Data fMix;
  Amplifier * psAmplifier;
  unsigned long lSampleIndex;

  psAmplifier = (Amplifier *)Instance;

  //gain knob logrithemic through input hint!
  fGain = *(psAmplifier->m_pfControlValue);
  fTHL = *(psAmplifier->m_pfTHLFreq);
  fTHH = *(psAmplifier->m_pfTHHFreq);
  fRoomSize = *(psAmplifier->m_pfRoomSize);
  fDetail = (unsigned long)*(psAmplifier->m_pfDetail);
  fDecay = *(psAmplifier->m_pfDecay);
  fMix = *(psAmplifier->m_pfMix);
  fEchoScale = *(psAmplifier->m_pfEchoScale);

  if (prev_freq1 != fTHL || prev_freq2 != fTHH){
    for (int i = 0; i <  magResponse.length/2; i++){
        float vala = 0.0;
        if (i > (int)fTHL && i < (int)fTHH) vala = 1.0;
        magResponse.asamples[i] = vala;
        magResponse.asamples[magResponse.length - i - 1] = vala;
    }
    getFirCofficients(&magResponse, &facs, FCOUNT);
    prev_freq1 = fTHL;
    prev_freq2 = fTHH;
  }

  if (prev_decay != fDecay || prev_echoscale != fEchoScale || prev_detail != fDetail){
    for (int i = 0; i < fDetail; i++){
        float tfactd =  fEchoScale * expf(-(fDecay)*(i+1)*0.15);
        reverbimpulses[i] = tfactd;
    }
    prev_echoscale = fEchoScale;
    prev_decay = fDecay;
  }

  if (prev_roomsize != fRoomSize || prev_detail != fDetail){
    impulsesampleseperation = (unsigned long)((((float)rate*TANKFOLDS)/((float)(fDetail+1) * MAX_ROOM_SIZE))* (float)fRoomSize);
    prev_roomsize = fRoomSize;
    prev_detail = fDetail;
  }

  pfInput = psAmplifier->m_pfInputBuffer1;
  pfOutput = psAmplifier->m_pfOutputBuffer1;

  for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++){
    if (saminc1 > rate*TANKFOLDS){
        saminc1 = (saminc1) - rate*TANKFOLDS;
        prevfilled1 = 1;
    }
    float val1 = 0;
    for (int i=0; i < (FCOUNT-1); i++) {
        val1 += facs[i] * ps1[i];
        ps1[i] = ps1[i+1];
    }
    ps1[FCOUNT-1] = pfInput[lSampleIndex];
    val1 += facs[FCOUNT-1]*pfInput[lSampleIndex];

    samplebuffer1[saminc1] = 0.5*val1;

    float revfac = 0;
    for (int k=0; k < fDetail; k++) {
        unsigned long reflectiontraceindex = k*impulsesampleseperation;
        if (prevfilled1 == 1){
            if (reflectiontraceindex < saminc1){
                revfac += reverbimpulses[k]*samplebuffer1[saminc1 - reflectiontraceindex];
            }
            else {
                revfac += reverbimpulses[k]*samplebuffer1[rate*TANKFOLDS-(reflectiontraceindex - saminc1)];
            }
        }
        else{
            if (reflectiontraceindex < saminc1){
                revfac += reverbimpulses[k]*samplebuffer1[saminc1 - reflectiontraceindex];
            }
        }
    }
    pfOutput[lSampleIndex] = fGain* ((1.0-fMix)*pfInput[lSampleIndex] + (fMix)*revfac);
    saminc1++;
  }

  pfInput = psAmplifier->m_pfInputBuffer2;
  pfOutput = psAmplifier->m_pfOutputBuffer2;

  for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++){
    float val2 = 0;
    if (saminc2 > rate*TANKFOLDS){
        saminc2 = (saminc2) - rate*TANKFOLDS;
        prevfilled2 = 1;
    }
    for (int i=0; i < (FCOUNT-1); i++) {
        val2 += facs[i] * ps2[i];
        ps2[i] = ps2[i+1];
    }
    ps2[FCOUNT-1] = pfInput[lSampleIndex];
    val2 += facs[FCOUNT-1]*pfInput[lSampleIndex];

    samplebuffer2[saminc2] = 0.5*val2;

    float revfac = 0;
    for (int k=0; k < fDetail; k++) {
        unsigned long reflectiontraceindex = k*impulsesampleseperation;
        if (prevfilled2 == 1){
            if (reflectiontraceindex < saminc2){
                revfac += reverbimpulses[k]*samplebuffer2[saminc2 - reflectiontraceindex];
            }
            else {
                revfac += reverbimpulses[k]*samplebuffer2[rate*TANKFOLDS-(reflectiontraceindex - saminc2)];
            }
        }
        else{
            if (reflectiontraceindex < saminc2){
                revfac += reverbimpulses[k]*samplebuffer2[saminc2 - reflectiontraceindex];
            }
        }
    }
    pfOutput[lSampleIndex] = fGain* ((1.0-fMix)*pfInput[lSampleIndex] + (fMix)*revfac);
    saminc2++;
  }
}


/* Throw away an amplifier. */
static void
cleanupAmplifier(LADSPA_Handle Instance) {
  free(Instance);
}

/*****************************************************************************/

LADSPA_Descriptor * g_psStereoDescriptor = NULL;

/*****************************************************************************/

/* Called automatically when the plugin library is first loaded. */
ON_LOAD_ROUTINE {
    char ** pcPortNames;
    LADSPA_PortDescriptor * piPortDescriptors;
    LADSPA_PortRangeHint * psPortRangeHints;

    g_psStereoDescriptor
      = (LADSPA_Descriptor *)malloc(sizeof(LADSPA_Descriptor));
    // setting up ports!
    if (g_psStereoDescriptor) {
      g_psStereoDescriptor->UniqueID
        = 1049;
      g_psStereoDescriptor->Label
        = strdup("Habre Delay Reverb");
      g_psStereoDescriptor->Properties
        = LADSPA_PROPERTY_HARD_RT_CAPABLE;
      g_psStereoDescriptor->Name
        = strdup("habre_delayreverb");
      g_psStereoDescriptor->Maker
        = strdup("Rhishma Pandey (rhishmapandey.com.np)");
      g_psStereoDescriptor->Copyright
        = strdup("Rhishma Pandey");
      g_psStereoDescriptor->PortCount
        = AMP_PORTS_COUNT;
      piPortDescriptors
        = (LADSPA_PortDescriptor *)calloc(AMP_PORTS_COUNT, sizeof(LADSPA_PortDescriptor));
      g_psStereoDescriptor->PortDescriptors
        = (const LADSPA_PortDescriptor *)piPortDescriptors;
      piPortDescriptors[AMP_CONTROL]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_THL]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_THH]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_DETAIL]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_DECAY]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_ECHOSCALE]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_ROOMSIZE]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_MIX]
        = LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL;
      piPortDescriptors[AMP_INPUT1]
        = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
      piPortDescriptors[AMP_OUTPUT1]
        = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
      piPortDescriptors[AMP_INPUT2]
        = LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO;
      piPortDescriptors[AMP_OUTPUT2]
        = LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO;
      pcPortNames
        = (char **)calloc(AMP_PORTS_COUNT, sizeof(char *));
      g_psStereoDescriptor->PortNames
        = (const char **)pcPortNames;
      pcPortNames[AMP_CONTROL]
        = strdup("Gain");
      pcPortNames[AMP_THL]
        = strdup("FreqStart");
      pcPortNames[AMP_THH]
        = strdup("FreqEnd");
      pcPortNames[AMP_DETAIL]
        = strdup("Compact");
      pcPortNames[AMP_DECAY]
        = strdup("Decay");
      pcPortNames[AMP_ECHOSCALE]
        = strdup("EchoScale");
      pcPortNames[AMP_ROOMSIZE]
        = strdup("RoomSize");
      pcPortNames[AMP_MIX]
        = strdup("Mix");
      pcPortNames[AMP_INPUT1]
        = strdup("Input (Left)");
      pcPortNames[AMP_OUTPUT1]
        = strdup("Output (Left)");
      pcPortNames[AMP_INPUT2]
        = strdup("Input (Right)");
      pcPortNames[AMP_OUTPUT2]
        = strdup("Output (Right)");

      // dynamic allocation for hints!

      psPortRangeHints = ((LADSPA_PortRangeHint *)
			calloc(AMP_PORTS_COUNT, sizeof(LADSPA_PortRangeHint)));
      g_psStereoDescriptor->PortRangeHints
        = (const LADSPA_PortRangeHint *)psPortRangeHints;

      // port hints
      psPortRangeHints[AMP_CONTROL].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_LOGARITHMIC
	 | LADSPA_HINT_DEFAULT_1
	 | LADSPA_HINT_BOUNDED_ABOVE);

      psPortRangeHints[AMP_THL].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_LOGARITHMIC
	 | LADSPA_HINT_DEFAULT_LOW);

      psPortRangeHints[AMP_THH].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_LOGARITHMIC
	 | LADSPA_HINT_DEFAULT_MAXIMUM);

      psPortRangeHints[AMP_DETAIL].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_DEFAULT_MINIMUM);

      psPortRangeHints[AMP_DECAY].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_DEFAULT_MIDDLE);

      psPortRangeHints[AMP_ECHOSCALE].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_DEFAULT_MAXIMUM);

      psPortRangeHints[AMP_ROOMSIZE].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_DEFAULT_MAXIMUM);


      psPortRangeHints[AMP_MIX].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_BOUNDED_BELOW
	 | LADSPA_HINT_DEFAULT_1);

      psPortRangeHints[AMP_CONTROL].LowerBound
        = 0;
      psPortRangeHints[AMP_CONTROL].UpperBound
        = 4;

      psPortRangeHints[AMP_THL].LowerBound
        = 0;
      psPortRangeHints[AMP_THL].UpperBound
        = 22000;

      psPortRangeHints[AMP_THH].LowerBound
        = 0;
      psPortRangeHints[AMP_THH].UpperBound
        = 22000;

      psPortRangeHints[AMP_DETAIL].LowerBound
        = 20;
      psPortRangeHints[AMP_DETAIL].UpperBound
        = AMP_MAX_REVERB_DETAIL;

      psPortRangeHints[AMP_DECAY].LowerBound
        = 1;
      psPortRangeHints[AMP_DECAY].UpperBound
        = 10;

      psPortRangeHints[AMP_ECHOSCALE].LowerBound
        = 0;
      psPortRangeHints[AMP_ECHOSCALE].UpperBound
        = 1;

      psPortRangeHints[AMP_ROOMSIZE].LowerBound
        = 1;
      psPortRangeHints[AMP_ROOMSIZE].UpperBound
        = MAX_ROOM_SIZE;


      psPortRangeHints[AMP_MIX].LowerBound
        = 0;
      psPortRangeHints[AMP_MIX].UpperBound
        = 1;

      psPortRangeHints[AMP_INPUT1].HintDescriptor
        = 0;
      psPortRangeHints[AMP_OUTPUT1].HintDescriptor
        = 0;
      psPortRangeHints[AMP_INPUT2].HintDescriptor
        = 0;
      psPortRangeHints[AMP_OUTPUT2].HintDescriptor
        = 0;
      g_psStereoDescriptor->instantiate
        = instantiateAmplifier;
      g_psStereoDescriptor->connect_port
        = connectPortToAmplifier;
      g_psStereoDescriptor->activate
        = NULL;
      g_psStereoDescriptor->run
        = runStereoEffect;
      g_psStereoDescriptor->run_adding
        = NULL;
      g_psStereoDescriptor->set_run_adding_gain
        = NULL;
      g_psStereoDescriptor->deactivate
        = NULL;
      g_psStereoDescriptor->cleanup
        = cleanupAmplifier;
    }
};

/*****************************************************************************/

static void
deleteDescriptor(LADSPA_Descriptor * psDescriptor) {
  unsigned long lIndex;
  if (psDescriptor) {
    free((char *)psDescriptor->Label);
    free((char *)psDescriptor->Name);
    free((char *)psDescriptor->Maker);
    free((char *)psDescriptor->Copyright);
    free((LADSPA_PortDescriptor *)psDescriptor->PortDescriptors);
    for (lIndex = 0; lIndex < psDescriptor->PortCount; lIndex++)
      free((char *)(psDescriptor->PortNames[lIndex]));
    free((char **)psDescriptor->PortNames);
    free((LADSPA_PortRangeHint *)psDescriptor->PortRangeHints);
    free(psDescriptor);
    free(facs);
    free(magResponse.asamples);
    free(reverbimpulses);
    free(samplebuffer1);
    free(samplebuffer2);
  }
}
/*****************************************************************************/

/* Called automatically when the library is unloaded. */
ON_UNLOAD_ROUTINE {
  deleteDescriptor(g_psStereoDescriptor);
}

/*****************************************************************************/

/* Return a descriptor of the requested plugin type. There are two
   plugin types available in this library (mono and stereo). */
const LADSPA_Descriptor *
ladspa_descriptor(unsigned long Index) {
  /* Return the requested descriptor or null if the index is out of
     range. */
  switch (Index) {
  case 0:
    return g_psStereoDescriptor;
  default:
    return NULL;
  }
}

/*****************************************************************************/

/* EOF */
