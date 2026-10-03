// fdlibm 5.3 na forma do V8 (src/base/ieee754.cc). Ver JsMath.h.
//
// ====================================================
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this
// software is freely granted, provided that this notice
// is preserved.
// ====================================================
#include "core/JsMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace rpg::js {
namespace {

// ---------------------------------------------------------------- acesso às palavras do double
std::uint64_t bits(double x) {
  std::uint64_t u;
  std::memcpy(&u, &x, sizeof u);
  return u;
}
double fromBits(std::uint64_t u) {
  double x;
  std::memcpy(&x, &u, sizeof x);
  return x;
}
std::int32_t highWord(double x) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(bits(x) >> 32)); }
std::uint32_t lowWord(double x) { return static_cast<std::uint32_t>(bits(x)); }
double insertWords(std::int32_t hi, std::uint32_t lo) {
  return fromBits((static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32) | lo);
}

// ---------------------------------------------------------------- redução de argumento
constexpr std::int32_t kTwoOverPi[] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7,
    0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129, 0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C,
    0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8, 0x97FFDE, 0x05980F, 0xEF2F11,
    0x8B5A0A, 0x6D1F6D, 0x367ECF, 0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5, 0xF17B3D, 0x0739F7,
    0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3, 0x91615E,
    0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880, 0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
};

constexpr std::int32_t kNpio2Hw[] = {
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
    0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
    0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB,
};

constexpr double kPIo2[] = {
    1.57079625129699707031e+00,  // 0x3FF921FB, 0x40000000
    7.54978941586159635335e-08,  // 0x3E74442D, 0x00000000
    5.39030252995776476554e-15,  // 0x3CF84698, 0x80000000
    3.28200341580791294123e-22,  // 0x3B78CC51, 0x60000000
    1.27065575308067607349e-29,  // 0x39F01B83, 0x80000000
    1.22933308981111328932e-36,  // 0x387A2520, 0x40000000
    2.73370053816464559624e-44,  // 0x36E38222, 0x80000000
    2.16741683877804819444e-51,  // 0x3569F31D, 0x00000000
};

// __kernel_rem_pio2: redução de argumentos grandes (|x| > 2^19·π/2) com prec = 2.
int kernelRemPio2(const double* x, double* y, int e0, int nx) {
  constexpr double two24 = 1.67772160000000000000e+07;   // 0x41700000, 0x00000000
  constexpr double twon24 = 5.96046447753906250000e-08;  // 0x3E700000, 0x00000000
  constexpr int jk = 4;                                  // init_jk[prec = 2]
  const int jp = jk;

  std::int32_t iq[20] = {};
  double f[20] = {}, fq[20] = {}, q[20] = {};

  const int jx = nx - 1;
  int jv = (e0 - 3) / 24;
  if (jv < 0) jv = 0;
  int q0 = e0 - 24 * (jv + 1);

  int j = jv - jx;
  const int m = jx + jk;
  for (int i = 0; i <= m; i++, j++) f[i] = j < 0 ? 0.0 : static_cast<double>(kTwoOverPi[j]);

  for (int i = 0; i <= jk; i++) {
    double fw = 0.0;
    for (j = 0; j <= jx; j++) fw += x[j] * f[jx + i - j];
    q[i] = fw;
  }

  int jz = jk;
  int n = 0, ih = 0;
  double z = 0.0;
  for (;;) {  // recompute:
    int i = 0;
    j = jz;
    z = q[jz];
    for (; j > 0; i++, j--) {
      const double fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
      iq[i] = static_cast<std::int32_t>(z - two24 * fw);
      z = q[j - 1] + fw;
    }

    z = std::scalbn(z, q0);
    z -= 8.0 * std::floor(z * 0.125);
    n = static_cast<std::int32_t>(z);
    z -= static_cast<double>(n);
    ih = 0;
    if (q0 > 0) {
      i = iq[jz - 1] >> (24 - q0);
      n += i;
      iq[jz - 1] -= i << (24 - q0);
      ih = iq[jz - 1] >> (23 - q0);
    } else if (q0 == 0) {
      ih = iq[jz - 1] >> 23;
    } else if (z >= 0.5) {
      ih = 2;
    }

    if (ih > 0) {
      n += 1;
      std::int32_t carry = 0;
      for (i = 0; i < jz; i++) {
        j = iq[i];
        if (carry == 0) {
          if (j != 0) {
            carry = 1;
            iq[i] = 0x1000000 - j;
          }
        } else {
          iq[i] = 0xffffff - j;
        }
      }
      if (q0 > 0) {
        switch (q0) {
          case 1: iq[jz - 1] &= 0x7fffff; break;
          case 2: iq[jz - 1] &= 0x3fffff; break;
          default: break;
        }
      }
      if (ih == 2) {
        z = 1.0 - z;
        if (carry != 0) z -= std::scalbn(1.0, q0);
      }
    }

    if (z == 0.0) {
      j = 0;
      for (i = jz - 1; i >= jk; i--) j |= iq[i];
      if (j == 0) {
        int k = 1;
        while (iq[jk - k] == 0) k++;
        for (i = jz + 1; i <= jz + k; i++) {
          f[jx + i] = static_cast<double>(kTwoOverPi[jv + i]);
          double fw = 0.0;
          for (j = 0; j <= jx; j++) fw += x[j] * f[jx + i - j];
          q[i] = fw;
        }
        jz += k;
        continue;
      }
    }
    break;
  }

  if (z == 0.0) {
    jz -= 1;
    q0 -= 24;
    while (iq[jz] == 0) {
      jz--;
      q0 -= 24;
    }
  } else {
    z = std::scalbn(z, -q0);
    if (z >= two24) {
      const double fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
      iq[jz] = static_cast<std::int32_t>(z - two24 * fw);
      jz += 1;
      q0 += 24;
      iq[jz] = static_cast<std::int32_t>(fw);
    } else {
      iq[jz] = static_cast<std::int32_t>(z);
    }
  }

  double fw = std::scalbn(1.0, q0);
  for (int i = jz; i >= 0; i--) {
    q[i] = fw * static_cast<double>(iq[i]);
    fw *= twon24;
  }

  for (int i = jz; i >= 0; i--) {
    fw = 0.0;
    for (int k = 0; k <= jp && k <= jz - i; k++) fw += kPIo2[k] * q[i + k];
    fq[jz - i] = fw;
  }

  fw = 0.0;
  for (int i = jz; i >= 0; i--) fw += fq[i];
  y[0] = ih == 0 ? fw : -fw;
  fw = fq[0] - fw;
  for (int i = 1; i <= jz; i++) fw += fq[i];
  y[1] = ih == 0 ? fw : -fw;
  return n & 7;
}

// __ieee754_rem_pio2: x − n·π/2 em y[0] + y[1]; devolve n.
std::int32_t remPio2(double x, double* y) {
  constexpr double half = 5.00000000000000000000e-01;
  constexpr double two24 = 1.67772160000000000000e+07;
  constexpr double invpio2 = 6.36619772367581382433e-01;  // 0x3FE45F30, 0x6DC9C883
  constexpr double pio2_1 = 1.57079632673412561417e+00;   // 0x3FF921FB, 0x54400000
  constexpr double pio2_1t = 6.07710050650619224932e-11;  // 0x3DD0B461, 0x1A626331
  constexpr double pio2_2 = 6.07710050630396597660e-11;   // 0x3DD0B461, 0x1A600000
  constexpr double pio2_2t = 2.02226624879595063154e-21;  // 0x3BA3198A, 0x2E037073
  constexpr double pio2_3 = 2.02226624871116645580e-21;   // 0x3BA3198A, 0x2E000000
  constexpr double pio2_3t = 8.47842766036889956997e-32;  // 0x397B839A, 0x252049C1

  const std::int32_t hx = highWord(x);
  const std::int32_t ix = hx & 0x7fffffff;
  if (ix <= 0x3fe921fb) {  // |x| ≤ π/4
    y[0] = x;
    y[1] = 0;
    return 0;
  }
  if (ix < 0x4002d97c) {  // |x| < 3π/4
    if (hx > 0) {
      double z = x - pio2_1;
      if (ix != 0x3ff921fb) {
        y[0] = z - pio2_1t;
        y[1] = (z - y[0]) - pio2_1t;
      } else {
        z -= pio2_2;
        y[0] = z - pio2_2t;
        y[1] = (z - y[0]) - pio2_2t;
      }
      return 1;
    }
    double z = x + pio2_1;
    if (ix != 0x3ff921fb) {
      y[0] = z + pio2_1t;
      y[1] = (z - y[0]) + pio2_1t;
    } else {
      z += pio2_2;
      y[0] = z + pio2_2t;
      y[1] = (z - y[0]) + pio2_2t;
    }
    return -1;
  }
  if (ix <= 0x413921fb) {  // |x| ≤ 2^19·(π/2)
    double t = std::fabs(x);
    const std::int32_t n = static_cast<std::int32_t>(t * invpio2 + half);
    const double fn = static_cast<double>(n);
    double r = t - fn * pio2_1;
    double w = fn * pio2_1t;
    if (n < 32 && ix != kNpio2Hw[n - 1]) {
      y[0] = r - w;
    } else {
      const std::int32_t j = ix >> 20;
      y[0] = r - w;
      std::uint32_t high = static_cast<std::uint32_t>(highWord(y[0]));
      std::int32_t i = j - static_cast<std::int32_t>((high >> 20) & 0x7ff);
      if (i > 16) {
        t = r;
        w = fn * pio2_2;
        r = t - w;
        w = fn * pio2_2t - ((t - r) - w);
        y[0] = r - w;
        high = static_cast<std::uint32_t>(highWord(y[0]));
        i = j - static_cast<std::int32_t>((high >> 20) & 0x7ff);
        if (i > 49) {
          t = r;
          w = fn * pio2_3;
          r = t - w;
          w = fn * pio2_3t - ((t - r) - w);
          y[0] = r - w;
        }
      }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) {
      y[0] = -y[0];
      y[1] = -y[1];
      return -n;
    }
    return n;
  }
  if (ix >= 0x7ff00000) {  // ∞ ou NaN
    y[0] = y[1] = x - x;
    return 0;
  }
  // z = scalbn(|x|, ilogb(x) − 23)
  const std::uint32_t low = lowWord(x);
  const std::int32_t e0 = (ix >> 20) - 1046;
  double z = insertWords(ix - static_cast<std::int32_t>(static_cast<std::uint32_t>(e0) << 20), low);
  double tx[3];
  for (int i = 0; i < 2; i++) {
    tx[i] = static_cast<double>(static_cast<std::int32_t>(z));
    z = (z - tx[i]) * two24;
  }
  tx[2] = z;
  int nx = 3;
  while (tx[nx - 1] == 0.0) nx--;
  const int n = kernelRemPio2(tx, y, e0, nx);
  if (hx < 0) {
    y[0] = -y[0];
    y[1] = -y[1];
    return -n;
  }
  return n;
}

// ---------------------------------------------------------------- núcleos em [−π/4, π/4]
double kernelSin(double x, double y, int iy) {
  constexpr double half = 5.00000000000000000000e-01;
  constexpr double S1 = -1.66666666666666324348e-01;  // 0xBFC55555, 0x55555549
  constexpr double S2 = 8.33333333332248946124e-03;   // 0x3F811111, 0x1110F8A6
  constexpr double S3 = -1.98412698298579493134e-04;  // 0xBF2A01A0, 0x19C161D5
  constexpr double S4 = 2.75573137070700676789e-06;   // 0x3EC71DE3, 0x57B1FE7D
  constexpr double S5 = -2.50507602534068634195e-08;  // 0xBE5AE5E6, 0x8A2B9CEB
  constexpr double S6 = 1.58969099521155010221e-10;   // 0x3DE5D93A, 0x5ACFD57C

  const std::int32_t ix = highWord(x) & 0x7fffffff;
  if (ix < 0x3e400000) {  // |x| < 2^-27
    if (static_cast<int>(x) == 0) return x;
  }
  const double z = x * x;
  const double v = z * x;
  const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
  if (iy == 0) return x + v * (S1 + z * r);
  return x - ((z * (half * y - v * r) - y) - v * S1);
}

double kernelCos(double x, double y) {
  constexpr double one = 1.00000000000000000000e+00;
  constexpr double C1 = 4.16666666666666019037e-02;   // 0x3FA55555, 0x5555554C
  constexpr double C2 = -1.38888888888741095749e-03;  // 0xBF56C16C, 0x16C15177
  constexpr double C3 = 2.48015872894767294178e-05;   // 0x3EFA01A0, 0x19CB1590
  constexpr double C4 = -2.75573143513906633035e-07;  // 0xBE927E4F, 0x809C52AD
  constexpr double C5 = 2.08757232129817482790e-09;   // 0x3E21EE9E, 0xBDB4B1C4
  constexpr double C6 = -1.13596475577881948265e-11;  // 0xBDA8FAE9, 0xBE8838D4

  const std::int32_t ix = highWord(x) & 0x7fffffff;
  if (ix < 0x3e400000) {  // |x| < 2^-27
    if (static_cast<int>(x) == 0) return one;
  }
  const double z = x * x;
  const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
  if (ix < 0x3FD33333) return one - (0.5 * z - (z * r - x * y));  // |x| < 0.3
  double qx;
  if (ix > 0x3fe90000) {  // |x| > 0.78125
    qx = 0.28125;
  } else {
    qx = insertWords(ix - 0x00200000, 0);  // x/4
  }
  const double hz = 0.5 * z - qx;
  const double a = one - qx;
  return a - (hz - (z * r - x * y));
}

}  // namespace

double sin(double x) {
  double y[2];
  const std::int32_t ix = highWord(x) & 0x7fffffff;
  if (ix <= 0x3fe921fb) return kernelSin(x, 0.0, 0);
  if (ix >= 0x7ff00000) return x - x;
  const std::int32_t n = remPio2(x, y);
  switch (n & 3) {
    case 0: return kernelSin(y[0], y[1], 1);
    case 1: return kernelCos(y[0], y[1]);
    case 2: return -kernelSin(y[0], y[1], 1);
    default: return -kernelCos(y[0], y[1]);
  }
}

double cos(double x) {
  double y[2];
  const std::int32_t ix = highWord(x) & 0x7fffffff;
  if (ix <= 0x3fe921fb) return kernelCos(x, 0.0);
  if (ix >= 0x7ff00000) return x - x;
  const std::int32_t n = remPio2(x, y);
  switch (n & 3) {
    case 0: return kernelCos(y[0], y[1]);
    case 1: return -kernelSin(y[0], y[1], 1);
    case 2: return -kernelCos(y[0], y[1]);
    default: return kernelSin(y[0], y[1], 1);
  }
}

double atan(double x) {
  constexpr double atanhi[] = {
      4.63647609000806093515e-01,  // atan(0.5)hi
      7.85398163397448278999e-01,  // atan(1.0)hi
      9.82793723247329054082e-01,  // atan(1.5)hi
      1.57079632679489655800e+00,  // atan(inf)hi
  };
  constexpr double atanlo[] = {
      2.26987774529616870924e-17,
      3.06161699786838301793e-17,
      1.39033110312309984516e-17,
      6.12323399573676603587e-17,
  };
  constexpr double aT[] = {
      3.33333333333329318027e-01,  -1.99999999998764832476e-01, 1.42857142725034663711e-01,
      -1.11111104054623557880e-01, 9.09088713343650656196e-02,  -7.69187620504482999495e-02,
      6.66107313738753120669e-02,  -5.83357013379057348645e-02, 4.97687799461593236017e-02,
      -3.65315727442169155270e-02, 1.62858201153657823623e-02,
  };
  constexpr double one = 1.0, huge = 1.0e300;

  const std::int32_t hx = highWord(x);
  const std::int32_t ix = hx & 0x7fffffff;
  int id;
  if (ix >= 0x44100000) {  // |x| ≥ 2^66
    const std::uint32_t low = lowWord(x);
    if (ix > 0x7ff00000 || (ix == 0x7ff00000 && low != 0)) return x + x;  // NaN
    if (hx > 0) return atanhi[3] + atanlo[3];
    return -atanhi[3] - atanlo[3];
  }
  if (ix < 0x3fdc0000) {  // |x| < 0.4375
    if (ix < 0x3e400000) {  // |x| < 2^-27
      if (huge + x > one) return x;
    }
    id = -1;
  } else {
    x = std::fabs(x);
    if (ix < 0x3ff30000) {    // |x| < 1.1875
      if (ix < 0x3fe60000) {  // 7/16 ≤ |x| < 11/16
        id = 0;
        x = (2.0 * x - one) / (2.0 + x);
      } else {  // 11/16 ≤ |x| < 19/16
        id = 1;
        x = (x - one) / (x + one);
      }
    } else {
      if (ix < 0x40038000) {  // |x| < 2.4375
        id = 2;
        x = (x - 1.5) / (one + 1.5 * x);
      } else {  // 2.4375 ≤ |x| < 2^66
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  const double z = x * x;
  const double w = z * z;
  const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
  const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
  if (id < 0) return x - x * (s1 + s2);
  const double r = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
  return hx < 0 ? -r : r;
}

double atan2(double y, double x) {
  constexpr double tiny = 1.0e-300;
  constexpr double pi_o_4 = 7.8539816339744827900E-01;  // 0x3FE921FB, 0x54442D18
  constexpr double pi_o_2 = 1.5707963267948965580E+00;  // 0x3FF921FB, 0x54442D18
  constexpr double pi = 3.1415926535897931160E+00;      // 0x400921FB, 0x54442D18
  constexpr double pi_lo = 1.2246467991473531772E-16;   // 0x3CA1A626, 0x33145C07

  const std::int32_t hx = highWord(x);
  const std::uint32_t lx = lowWord(x);
  const std::int32_t ix = hx & 0x7fffffff;
  const std::int32_t hy = highWord(y);
  const std::uint32_t ly = lowWord(y);
  const std::int32_t iy = hy & 0x7fffffff;
  const auto nanPart = [](std::uint32_t l) { return static_cast<std::int32_t>((l | (0u - l)) >> 31); };
  if ((ix | nanPart(lx)) > 0x7ff00000 || (iy | nanPart(ly)) > 0x7ff00000) return x + y;  // NaN
  // x = 1.0 (a subtração é feita sem sinal: no fdlibm ela dá a volta, em C++ com sinal seria UB)
  if (((static_cast<std::uint32_t>(hx) - 0x3ff00000u) | lx) == 0) return atan(y);
  int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);                                       // 2·sinal(x) + sinal(y)

  if ((iy | static_cast<std::int32_t>(ly)) == 0) {  // y = 0
    switch (m) {
      case 0:
      case 1: return y;
      case 2: return pi + tiny;
      default: return -pi - tiny;
    }
  }
  if ((ix | static_cast<std::int32_t>(lx)) == 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;  // x = 0

  if (ix == 0x7ff00000) {  // x = ±∞
    if (iy == 0x7ff00000) {
      switch (m) {
        case 0: return pi_o_4 + tiny;
        case 1: return -pi_o_4 - tiny;
        case 2: return 3.0 * pi_o_4 + tiny;
        default: return -3.0 * pi_o_4 - tiny;
      }
    }
    switch (m) {
      case 0: return 0.0;
      case 1: return -0.0;
      case 2: return pi + tiny;
      default: return -pi - tiny;
    }
  }
  if (iy == 0x7ff00000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;  // y = ±∞

  const std::int32_t k = (iy - ix) >> 20;
  double z;
  if (k > 60) {  // |y/x| > 2^60
    z = pi_o_2 + 0.5 * pi_lo;
    m &= 1;
  } else if (hx < 0 && k < -60) {
    z = 0.0;
  } else {
    z = atan(std::fabs(y / x));
  }
  switch (m) {
    case 0: return z;
    case 1: return -z;
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
  }
}

double exp(double x) {
  constexpr double one = 1.0;
  constexpr double halF[2] = {0.5, -0.5};
  constexpr double huge = 1.0e+300;
  constexpr double o_threshold = 7.09782712893383973096e+02;  // 0x40862E42, 0xFEFA39EF
  constexpr double u_threshold = -7.45133219101941108420e+02;  // 0xc0874910, 0xD52D3051
  constexpr double ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01};
  constexpr double ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10};
  constexpr double invln2 = 1.44269504088896338700e+00;  // 0x3ff71547, 0x652b82fe
  constexpr double P1 = 1.66666666666666019037e-01;      // 0x3FC55555, 0x5555553E
  constexpr double P2 = -2.77777777770155933842e-03;     // 0xBF66C16C, 0x16BEBD93
  constexpr double P3 = 6.61375632143793436117e-05;      // 0x3F11566A, 0xAF25DE2C
  constexpr double P4 = -1.65339022054652515390e-06;     // 0xBEBBBD41, 0xC5D26BF1
  constexpr double P5 = 4.13813679705723846039e-08;      // 0x3E663769, 0x72BEA4D0
  const double twom1000 = 9.33263618503218878990e-302;   // 2^-1000

  double hi = 0.0, lo = 0.0;
  int k = 0;
  std::uint32_t hx = static_cast<std::uint32_t>(highWord(x));
  const int xsb = static_cast<int>((hx >> 31) & 1);
  hx &= 0x7fffffff;

  if (hx >= 0x40862E42) {  // |x| ≥ 709.78…
    if (hx >= 0x7ff00000) {
      const std::uint32_t lx = lowWord(x);
      if (((hx & 0xfffff) | lx) != 0) return x + x;  // NaN
      return xsb == 0 ? x : 0.0;                      // exp(±∞) = {∞, 0}
    }
    if (x > o_threshold) return huge * huge;
    if (x < u_threshold) return twom1000 * twom1000;
  }

  if (hx > 0x3fd62e42) {    // |x| > 0,5·ln 2
    if (hx < 0x3FF0A2B2) {  // e |x| < 1,5·ln 2
      hi = x - ln2HI[xsb];
      lo = ln2LO[xsb];
      k = 1 - xsb - xsb;
    } else {
      k = static_cast<int>(invln2 * x + halF[xsb]);
      const double t = k;
      hi = x - t * ln2HI[0];
      lo = t * ln2LO[0];
    }
    x = hi - lo;
  } else if (hx < 0x3e300000) {  // |x| < 2^-28
    if (huge + x > one) return one + x;
  } else {
    k = 0;
  }

  const double t = x * x;
  double twopk;
  if (k >= -1021) {
    twopk = insertWords(0x3ff00000 + (k << 20), 0);
  } else {
    twopk = insertWords(0x3ff00000 + ((k + 1000) << 20), 0);
  }
  const double c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  if (k == 0) return one - ((x * c) / (c - 2.0) - x);
  const double y = one - ((lo - (x * c) / (2.0 - c)) - hi);
  if (k >= -1021) {
    if (k == 1024) return y * 2.0 * 8.98846567431157953865e+307;  // 0x1p1023
    return y * twopk;
  }
  return y * twopk * twom1000;
}

}  // namespace rpg::js
