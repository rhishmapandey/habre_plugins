#pragma once
#include <math.h>

typedef struct{
  float* asamples;
  unsigned long length;
} SLIB_SAMPLES;

void getFirCofficients(SLIB_SAMPLES* magResponse, float **res ,unsigned long count) {
    float ffac = M_PI*2.0/magResponse->length;
    for (int i = 0; i < count; i++) {
        float fac = 0;
        for (int j = 0; j < magResponse->length; j++) {
            fac += (1.0/magResponse->length)* magResponse->asamples[j] * cos(ffac*i*j);
        }
        (*res)[i] = fac;
    }
}
