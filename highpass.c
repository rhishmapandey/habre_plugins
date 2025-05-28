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
#define AMP_THH 5

#define AMP_PORTS_COUNT 6
/*****************************************************************************/

/* The structure used to hold port connection information and state
   (actually gain controls require no further state). */
static unsigned int rate;
static unsigned long saminc;

#define FCOUNT 25

static float ps1[FCOUNT];
static float ps2[FCOUNT];
float* facs;
SLIB_SAMPLES magResponse;
float prev_freq = -9999;

typedef struct {

  /* Ports:
     ------ */

  LADSPA_Data * m_pfControlValue;
  LADSPA_Data * m_pfTHHFreq;
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
  saminc = 0;
  facs = (float*)malloc(sizeof(float)*FCOUNT);
  magResponse.asamples = (float*)malloc(sizeof(float)*SampleRate);
  magResponse.length = SampleRate;
  for (int i = 0; i < FCOUNT; i++) {
    ps1[0] = 0.0;
    ps2[0] = 0.0;
  }
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
  case AMP_THH:
    psAmplifier->m_pfTHHFreq = DataLocation;
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

/*****************************************************************************/

static void
runStereoEffect(LADSPA_Handle Instance,
		   unsigned long SampleCount) {

  LADSPA_Data * pfInput;
  LADSPA_Data * pfOutput;
  LADSPA_Data fGain;
  LADSPA_Data fTHH;
  Amplifier * psAmplifier;
  unsigned long lSampleIndex;

  psAmplifier = (Amplifier *)Instance;

  //gain knob logrithemic through input hint!
  fGain = *(psAmplifier->m_pfControlValue);
  fTHH = *(psAmplifier->m_pfTHHFreq);
  if (saminc > rate){
    saminc = (saminc) - rate;
  }

  if (prev_freq != fTHH){
    for (int i = 0; i <  magResponse.length/2; i++){
        float vala = 0.0;
        if (i > (int)fTHH) vala = 1.0;
        magResponse.asamples[i] = vala;
        magResponse.asamples[magResponse.length - i - 1] = vala;
    }
    getFirCofficients(&magResponse, &facs, FCOUNT);
    prev_freq = fTHH;
  }


  pfInput = psAmplifier->m_pfInputBuffer1;
  pfOutput = psAmplifier->m_pfOutputBuffer1;
  //LADSPA_Data sfac = (ceil(fTHL)*M_PI*2.0)/(rate);

  for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++){
    float val1 = 0;
    for (int i=0; i < (FCOUNT-1); i++) {
        val1 += facs[i] * ps1[i];
        ps1[i] = ps1[i+1];
    }
    ps1[FCOUNT-1] = pfInput[lSampleIndex];
    val1 += facs[FCOUNT-1]*pfInput[lSampleIndex];
        pfOutput[lSampleIndex] = fGain*val1;
  }
  pfInput = psAmplifier->m_pfInputBuffer2;
  pfOutput = psAmplifier->m_pfOutputBuffer2;
  for (lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++){
    float val2 = 0;
    for (int i=0; i < (FCOUNT-1); i++) {
        val2 += facs[i] * ps2[i];
        ps2[i] = ps2[i+1];
    }
    ps2[FCOUNT-1] = pfInput[lSampleIndex];
    val2 += facs[FCOUNT-1]*pfInput[lSampleIndex];
    pfOutput[lSampleIndex] = fGain*val2;
  }
  saminc += SampleCount;
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
        = strdup("Habre High Pass");
      g_psStereoDescriptor->Properties
        = LADSPA_PROPERTY_HARD_RT_CAPABLE;
      g_psStereoDescriptor->Name
        = strdup("habre_highpass");
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
      piPortDescriptors[AMP_THH]
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
      pcPortNames[AMP_THH]
        = strdup("Threshold");
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
      psPortRangeHints[AMP_CONTROL].LowerBound
        = 0;
      psPortRangeHints[AMP_CONTROL].UpperBound
        = 4;

      psPortRangeHints[AMP_THH].HintDescriptor
        = (LADSPA_HINT_BOUNDED_BELOW
     | LADSPA_HINT_BOUNDED_ABOVE
	 | LADSPA_HINT_LOGARITHMIC
	 | LADSPA_HINT_DEFAULT_MINIMUM);

      psPortRangeHints[AMP_THH].LowerBound
        = 0;
      psPortRangeHints[AMP_THH].UpperBound
        = 22000;

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
