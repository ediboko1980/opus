/* (C) 2007 Jean-Marc Valin, CSIRO
*/
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include "bands.h"
#include "modes.h"
#include "vq.h"
#include "cwrs.h"

/* Applies a series of rotations so that pulses are spread like a two-sided
exponential */
static void exp_rotation(float *X, int len, float theta, int dir)
{
   int i, k, stride=2;
   float c, s;
   c = cos(theta);
   s = sin(theta);
   if (dir > 0)
   {
      for (i=0;i<len-2;i++)
      {
         float x1, x2;
         x1 = X[i];
         x2 = X[i+2];
         X[i] = c*x1 - s*x2;
         X[i+2] = c*x2 + s*x1;
      }
      for (i=len-5;i>=0;i--)
      {
         float x1, x2;
         x1 = X[i];
         x2 = X[i+2];
         X[i] = c*x1 - s*x2;
         X[i+2] = c*x2 + s*x1;
      }

   } else {
      for (i=0;i<len-4;i++)
      {
         float x1, x2;
         x1 = X[i];
         x2 = X[i+2];
         X[i] = c*x1 + s*x2;
         X[i+2] = c*x2 - s*x1;
      }
      
      for (i=len-3;i>=0;i--)
      {
         float x1, x2;
         x1 = X[i];
         x2 = X[i+2];
         X[i] = c*x1 + s*x2;
         X[i+2] = c*x2 - s*x1;
      }
   }
}


/* Compute the energy in each of the bands */
void compute_band_energies(const CELTMode *m, float *X, float *bank)
{
   int i, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   for (i=0;i<m->nbEBands;i++)
   {
      int j;
      bank[i] = 1e-10;
      for (j=B*eBands[i];j<B*eBands[i+1];j++)
         bank[i] += X[j]*X[j];
      bank[i] = sqrt(bank[i]);
   }
}

/* Normalise each band such that the energy is one. */
void normalise_bands(const CELTMode *m, float *X, float *bank)
{
   int i, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   for (i=0;i<m->nbEBands;i++)
   {
      int j;
      float x = 1.f/(1e-10+bank[i]);
      for (j=B*eBands[i];j<B*eBands[i+1];j++)
         X[j] *= x;
   }
   for (i=B*eBands[m->nbEBands];i<B*eBands[m->nbEBands+1];i++)
      X[i] = 0;
}

/* De-normalise the energy to produce the synthesis from the unit-energy bands */
void denormalise_bands(const CELTMode *m, float *X, float *bank)
{
   int i, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   for (i=0;i<m->nbEBands;i++)
   {
      int j;
      float x = bank[i];
      for (j=B*eBands[i];j<B*eBands[i+1];j++)
         X[j] *= x;
   }
   for (i=B*eBands[m->nbEBands];i<B*eBands[m->nbEBands+1];i++)
      X[i] = 0;
}


/* Compute the best gain for each "pitch band" */
void compute_pitch_gain(const CELTMode *m, float *X, float *P, float *gains, float *bank)
{
   int i, B;
   const int *eBands = m->eBands;
   const int *pBands = m->pBands;
   B = m->nbMdctBlocks*m->nbChannels;
   float w[B*eBands[m->nbEBands]];
   for (i=0;i<m->nbEBands;i++)
   {
      int j;
      for (j=B*eBands[i];j<B*eBands[i+1];j++)
         w[j] = bank[i];
   }

   
   for (i=0;i<m->nbPBands;i++)
   {
      float Sxy=0;
      float Sxx = 0;
      int j;
      float gain;
      for (j=B*pBands[i];j<B*pBands[i+1];j++)
      {
         Sxy += X[j]*P[j]*w[j];
         Sxx += X[j]*X[j]*w[j];
      }
      gain = Sxy/(1e-10+Sxx);
      //gain = Sxy/(2*(pbank[i+1]-pbank[i]));
      //if (i<3)
      //gain *= 1+.02*gain;
      if (gain > .90)
         gain = .90;
      if (gain < 0.0)
         gain = 0.0;
      
      gains[i] = gain;
   }
   for (i=B*pBands[m->nbPBands];i<B*pBands[m->nbPBands+1];i++)
      P[i] = 0;
}

/* Apply the (quantised) gain to each "pitch band" */
void pitch_quant_bands(const CELTMode *m, float *X, float *P, float *gains)
{
   int i, B;
   const int *pBands = m->pBands;
   B = m->nbMdctBlocks*m->nbChannels;
   for (i=0;i<m->nbPBands;i++)
   {
      int j;
      for (j=B*pBands[i];j<B*pBands[i+1];j++)
         P[j] *= gains[i];
      //printf ("%f ", gain);
   }
   for (i=B*pBands[m->nbPBands];i<B*pBands[m->nbPBands+1];i++)
      P[i] = 0;
}

void quant_bands(const CELTMode *m, float *X, float *P, float *W, ec_enc *enc)
{
   int i, j, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   float norm[B*eBands[m->nbEBands+1]];
   
   for (i=0;i<m->nbEBands;i++)
   {
      int q;
      q = m->nbPulses[i];
      if (q>0) {
         float n = sqrt(B*(eBands[i+1]-eBands[i]));
         alg_quant(X+B*eBands[i], W+B*eBands[i], B*(eBands[i+1]-eBands[i]), q, P+B*eBands[i], 0.7, enc);
         for (j=B*eBands[i];j<B*eBands[i+1];j++)
            norm[j] = X[j] * n;
         //printf ("%f ", log2(ncwrs64(B*(eBands[i+1]-eBands[i]), q))/(B*(eBands[i+1]-eBands[i])));
         //printf ("%f ", log2(ncwrs64(B*(eBands[i+1]-eBands[i]), q)));
      } else {
         float n = sqrt(B*(eBands[i+1]-eBands[i]));
         copy_quant(X+B*eBands[i], W+B*eBands[i], B*(eBands[i+1]-eBands[i]), -q, norm, B, eBands[i], enc);
         for (j=B*eBands[i];j<B*eBands[i+1];j++)
            norm[j] = X[j] * n;
         //printf ("%f ", (1+log2(eBands[i]-(eBands[i+1]-eBands[i]))+log2(ncwrs64(B*(eBands[i+1]-eBands[i]), -q)))/(B*(eBands[i+1]-eBands[i])));
         //printf ("%f ", (1+log2(eBands[i]-(eBands[i+1]-eBands[i]))+log2(ncwrs64(B*(eBands[i+1]-eBands[i]), -q))));
      }
   }
   //printf ("\n");
   for (i=B*eBands[m->nbEBands];i<B*eBands[m->nbEBands+1];i++)
      X[i] = 0;
}

void unquant_bands(const CELTMode *m, float *X, float *P, ec_dec *dec)
{
   int i, j, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   float norm[B*eBands[m->nbEBands+1]];
   
   for (i=0;i<m->nbEBands;i++)
   {
      int q;
      q = m->nbPulses[i];
      if (q>0) {
         float n = sqrt(B*(eBands[i+1]-eBands[i]));
         alg_unquant(X+B*eBands[i], B*(eBands[i+1]-eBands[i]), q, P+B*eBands[i], 0.7, dec);
         for (j=B*eBands[i];j<B*eBands[i+1];j++)
            norm[j] = X[j] * n;
      } else {
         float n = sqrt(B*(eBands[i+1]-eBands[i]));
         for (j=B*eBands[i];j<B*eBands[i+1];j++)
            X[j] = 0;
         copy_unquant(X+B*eBands[i], B*(eBands[i+1]-eBands[i]), -q, norm, B, eBands[i], dec);
         for (j=B*eBands[i];j<B*eBands[i+1];j++)
            norm[j] = X[j] * n;
      }
   }
   for (i=B*eBands[m->nbEBands];i<B*eBands[m->nbEBands+1];i++)
      X[i] = 0;
}

void band_rotation(const CELTMode *m, float *X, int dir)
{
   int i, B;
   const int *eBands = m->eBands;
   B = m->nbMdctBlocks*m->nbChannels;
   for (i=0;i<m->nbEBands;i++)
   {
      float theta;
      theta = pow(.1f,1.f*abs(m->nbPulses[i])/(B*(eBands[i+1]-eBands[i])));
      exp_rotation(X+B*eBands[i], B*(eBands[i+1]-eBands[i]), theta, dir);
   }
   //printf ("\n");
}
