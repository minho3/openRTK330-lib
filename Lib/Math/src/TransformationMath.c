/*
 * File:   TransformationMath.cpp
 * Author: joemotyka
 *
 * Created on May 8, 2016, 12:35 AM
 */
#include <stdio.h>
#include <math.h>
#include "TransformationMath.h"
#include "MatrixMath.h"
#include "VectorMath.h"
#include "Indices.h"

#if FAST_MATH
#include "arm_math.h"
#include "FastInvTrigFuncs.h"
#endif // FAST_MATH

 /******************************************************************************
 * @brief Calculate magnetic field in a perpendicular frame.
 * This perpendicular frame is generated by rotating the navigation frame about
 * its Z axis for YAW degree. The perpendicular frame is an intermediate frame
 * from the navigation frame to the body frame. Let v be a vector. v is described
 * in the navigation frame, the perpendicular frame and the body frame by v_n,
 * v_p and v_b, respectively. The following equation hold:
 *   v_b = Rx(roll) * Ry(pitch) * Rz(yaw) * v_n
 *   v_b = Rx(roll) * Ry(pitch) * v_p
 * In this routine, the gravity vector in the body frame and the magnetic vector
 * in the body frame are known, the magnetic vector in the perpendicular frame
 * need to be calculated.
 * TRACE:
 * @param [in] MagFieldVector    magnetic vector in the body frame.
 * @param [in] unitGravityVector unit gravity vector in the body frame.
 *   note: accelerometer measurement = -gravity when there is no linear acceleration.
 *   If acceleromter measurement is [0; 0; -1], the unit gravity vector should
 *   be [0; 0; 1].
 * @param [out] nedMagFieldVector    magnetic vector in the NED frame.
 * @retval 1 if magnetometers are used, otherwise it returns a zero.
 ******************************************************************************/
static void _TransformMagFieldToPerpFrame( real* MagFieldVector,
                                           real* nedMagFieldVector,
                                           real* unitGravityVector );

#define  CORRECT_IN_BODY_FRAME

//=============================================================================
void UnitGravity(real *accel, real *unitGravityVector)
{
    real accelMag = (real)sqrt( accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2] );
    real invAccelMag = 1.0f / accelMag;
    unitGravityVector[0] = -accel[0] * invAccelMag;
    unitGravityVector[1] = -accel[1] * invAccelMag;
    unitGravityVector[2] = -accel[2] * invAccelMag;
}

void UnitGravityToEulerAngles(real *unitGravityVector, real* eulerAngles)
{
    eulerAngles[ROLL]  = (real)( atan2( unitGravityVector[Y_AXIS],
                                        unitGravityVector[Z_AXIS] ) );
    eulerAngles[PITCH] = (real)( -asin( unitGravityVector[X_AXIS] ) );
}

real UnitGravityAndMagToYaw(real *unitGravityVector, real *magFieldVector)
{
    real nedMagFieldVector[3] = {0.0};
    // Transform the magnetic field vector from the body-frame to the plane normal to the gravity vector
    _TransformMagFieldToPerpFrame( magFieldVector, nedMagFieldVector, unitGravityVector );

    // The negative of the angle the vector makes with the unrotated (psi = 0)
    //   frame is the yaw-angle of the initial frame.
    return (real)( -atan2( nedMagFieldVector[Y_AXIS], nedMagFieldVector[X_AXIS] ) );
}

real RollPitchAndMagToYaw(real roll, real pitch, real *magFieldVector)
{
    real nedMagFieldVector[3] = {0.0};
    
    real sinRoll, cosRoll;
    real sinPitch, cosPitch;
    real temp;

    sinRoll  = (real)(sin( roll ));
    cosRoll  = (real)(cos( roll ));
    sinPitch = (real)(sin( pitch ));
    cosPitch = (real)(cos( pitch ));

    temp = sinRoll * magFieldVector[Y_AXIS] + cosRoll * magFieldVector[Z_AXIS];

    nedMagFieldVector[X_AXIS] =  cosPitch * magFieldVector[X_AXIS] + sinPitch * temp;
    nedMagFieldVector[Y_AXIS] =  cosRoll  * magFieldVector[Y_AXIS] - sinRoll  * magFieldVector[Z_AXIS];
    //nedMagFieldVector[Z_AXIS] = -sinPitch * magFieldVector[X_AXIS] + cosPitch * temp;

    return (real)( -atan2( nedMagFieldVector[Y_AXIS], nedMagFieldVector[X_AXIS] ) );
}

static void _TransformMagFieldToPerpFrame( real* magFieldVector,
                                           real* nedMagFieldVector,
                                           real* unitGravityVector )
{
    real sinRoll, cosRoll;
    real sinPitch, cosPitch;
    real temp;

    sinPitch = -unitGravityVector[0];
    if ( sinPitch >= 1.0 )          // roll and yaw undefined, assume roll = 0
    {
        nedMagFieldVector[0] = magFieldVector[2];
        nedMagFieldVector[1] = magFieldVector[1];
        nedMagFieldVector[2] = -magFieldVector[0];
        // set up some kind of flag to return
    }
    else if ( sinPitch <= -1.0 )    // roll and yaw undefined, assume roll = 0
    {
        nedMagFieldVector[0] = -magFieldVector[2];
        nedMagFieldVector[1] = magFieldVector[1];
        nedMagFieldVector[2] = magFieldVector[0];
        // set up some kind of flag to return
    }
    else
    {
        cosPitch = sqrtf( 1.0f - sinPitch*sinPitch );   // pitch is (-90, 90), cos(pitch)>0
        sinRoll  = unitGravityVector[1] / cosPitch;
        cosRoll  = unitGravityVector[2] / cosPitch;
        temp = sinRoll * magFieldVector[1] + cosRoll * magFieldVector[2];
        nedMagFieldVector[0] =  cosPitch * magFieldVector[0] + sinPitch * temp;
        nedMagFieldVector[1] =  cosRoll  * magFieldVector[1] - sinRoll  * magFieldVector[2];
        nedMagFieldVector[2] = -sinPitch * magFieldVector[2] + cosPitch * temp;
    }
    // return something to indicate pitch = 90
}

///*****************************************************************************
//* @name LLA_To_R_EinN returns the rotation matrix that converts from the Earth-
//*                     Centered, Earth-Fixed [m] to the North/East/Down-Frame [m]
//*
//* @details Pre calculated all non-changing constants and unfolded the matrices
//* @param [in] LLA - array with the Latitude, Longitude and Altitude [rad]
//* @param [out] R_EinN - rotation matrix from ECEF to NED
//* @retval always 1
//******************************************************************************/
BOOL LLA_To_R_EinN( double* llaRad,
                    real*  R_EinN )
{
    real sinLat, cosLat;
    real sinLon, cosLon;

    sinLat = (real)sin(llaRad[LAT]);
    cosLat = (real)cos(llaRad[LAT]);
    sinLon = (real)sin(llaRad[LON]);
    cosLon = (real)cos(llaRad[LON]);

    // First row
    *(R_EinN + 0 * 3 + 0) = -sinLat * cosLon;
    *(R_EinN + 0 * 3 + 1) = -sinLat * sinLon;
    *(R_EinN + 0 * 3 + 2) =  cosLat;

    // Second row
    *(R_EinN + 1 * 3 + 0) = -sinLon;
    *(R_EinN + 1 * 3 + 1) =  cosLon;
    *(R_EinN + 1 * 3 + 2) =  0.0;

    // Third row
    *(R_EinN + 2 * 3 + 0) = -cosLat * cosLon;
    *(R_EinN + 2 * 3 + 1) = -cosLat * sinLon;
    *(R_EinN + 2 * 3 + 2) = -sinLat;

    return 1;
}


///** ***************************************************************************
//* @name LLA_To_R_NinE returns a rotation matrix North [m], East [m] down [m] to
//*       Earth Centered Earth Fixed [m] coordinates
//* @details Pre calculated all non-changing constants and unfolded the matrices
//* @param [in] LLA - array with the Latitude, Longitude and Altitude [rad]
//* @param [out] InvRne - rotation matrix from NED to ECEF
//* @retval always 1
//******************************************************************************/
BOOL LLA_To_R_NinE( double* llaRad,
                    real* R_NinE )
{
    real sinLat, cosLat;
    real sinLon, cosLon;

    sinLat = (real)(sin((real)llaRad[LAT]));
    cosLat = (real)(cos((real)llaRad[LAT]));
    sinLon = (real)(sin((real)llaRad[LON]));
    cosLon = (real)(cos((real)llaRad[LON]));

    *(R_NinE + 0 * 3 + 0) = -sinLat * cosLon;
    *(R_NinE + 0 * 3 + 1) = -sinLon;
    *(R_NinE + 0 * 3 + 2) = -cosLat * cosLon;

    *(R_NinE + 1 * 3 + 0) = -sinLat * sinLon;
    *(R_NinE + 1 * 3 + 1) =  cosLon;
    *(R_NinE + 1 * 3 + 2) = -cosLat * sinLon;

    *(R_NinE + 2 * 3 + 0) =  cosLat;
    *(R_NinE + 2 * 3 + 1) =  0.0;
    *(R_NinE + 2 * 3 + 2) = -sinLat;

    return 1;
}


///** ***************************************************************************
//* @name LLA2Base Express LLA in a local NED Base Frame
//* @details Pre calculated all non-changing constants and unfolded the matrices
//* @param [in]  LLA - array with the current Latitude, Longitude and Altitude [rad]
//* @param [in]  BaseECEF - start of frame position
//* @param [in]  Rne - rotation matrix from ECEF to NED
//* @param [out] NED - output of the position in North East and Down coords
//* @param [out] newECEF - current position in ECEF from LLA
//* @retval always 1
//******************************************************************************/
BOOL LLA_To_Base( double* llaRad,  // in
                  double* rECEF_Init,  // in
                  real* dr_N,  //NED,
                  real* R_NinE,
                  double* rECEF)  // out
{
    real dr_E[NUM_AXIS];

    double N;
    double sinLat = sin(llaRad[LAT]);
    double cosLat = cos(llaRad[LAT]);
    double sinLon = sin(llaRad[LON]);
    double cosLon = cos(llaRad[LON]);

    real sinLat_r = (real)sinLat;
    real cosLat_r = (real)cosLat;
    real sinLon_r = (real)sinLon;
    real cosLon_r = (real)cosLon;

    N = E_MAJOR / sqrt(1.0 - (E_ECC_SQ * sinLat * sinLat)); // radius of Curvature [meters]

    //LLA_To_ECEF(llaRad, rECEF);
    double temp_d = (N + llaRad[ALT]) * cosLat;
    rECEF[X_AXIS] = temp_d * cosLon;
    rECEF[Y_AXIS] = temp_d * sinLon;
    rECEF[Z_AXIS] = ((E_MINOR_OVER_MAJOR_SQ * N) + llaRad[ALT]) * sinLat;

    dr_E[X_AXIS] = (real)( rECEF[X_AXIS] - *(rECEF_Init + X_AXIS) );
    dr_E[Y_AXIS] = (real)( rECEF[Y_AXIS] - *(rECEF_Init + Y_AXIS) );
    dr_E[Z_AXIS] = (real)( rECEF[Z_AXIS] - *(rECEF_Init + Z_AXIS) );

    // Form R_NinE
    // First row
    *(R_NinE + 0 * 3 + 0) = -sinLat_r * cosLon_r;
    *(R_NinE + 0 * 3 + 1) = -sinLon_r;
    *(R_NinE + 0 * 3 + 2) = -cosLat_r * cosLon_r;

    // Second row
    *(R_NinE + 1 * 3 + 0) = -sinLat_r * sinLon_r;
    *(R_NinE + 1 * 3 + 1) =  cosLon_r;
    *(R_NinE + 1 * 3 + 2) = -cosLat_r * sinLon_r;

    // Third row
    *(R_NinE + 2 * 3 + 0) =  cosLat_r;
    *(R_NinE + 2 * 3 + 1) =       0.0;
    *(R_NinE + 2 * 3 + 2) = -sinLat_r;

    // Convert from delta-position in the ECEF-frame to the NED-frame (the transpose
    //   in the equations that followed is handled in the formulation)
    //
    //       N E          ( E N )T
    // dr_N = R  * dr_E = (  R  )  * dr_E
    //                    (     )
    dr_N[X_AXIS] = *(R_NinE + X_AXIS * 3 + X_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + X_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + X_AXIS) * dr_E[Z_AXIS];
    dr_N[Z_AXIS] = *(R_NinE + X_AXIS * 3 + Z_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + Z_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + Z_AXIS) * dr_E[Z_AXIS];
    dr_N[Y_AXIS] = *(R_NinE + X_AXIS * 3 + Y_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + Y_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + Y_AXIS) * dr_E[Z_AXIS];

    return 1;
}

///** ***************************************************************************
//* @name Calculate NED relative position of two ECEF positions.
//* @details Pre calculated all non-changing constants and unfolded the matrices
//* @param [in]     rECEF_Init - start of frame position
//* @param [in]     rECEF - current position in ECEF from LLA
//* @param [in]     R_NinE - rotation matrix from NED to ECEF
//* @param [out]    dr_N - output of the position in North East and Down coords
//* @retval always 1
//******************************************************************************/
BOOL ECEF_To_Base( double* rECEF_Init,
                  double* rECEF,
                  real* R_NinE,
                  real* dr_N)
{
    real dr_E[NUM_AXIS];

    dr_E[X_AXIS] = (real)( rECEF[X_AXIS] - *(rECEF_Init + X_AXIS) );
    dr_E[Y_AXIS] = (real)( rECEF[Y_AXIS] - *(rECEF_Init + Y_AXIS) );
    dr_E[Z_AXIS] = (real)( rECEF[Z_AXIS] - *(rECEF_Init + Z_AXIS) );

    /* Convert from delta-position in the ECEF-frame to the NED-frame (the transpose
     * in the equations that followed is handled in the formulation)
     *
     *       N E          ( E N )T
     * dr_N = R  * dr_E = (  R  )  * dr_E
     *                    (     )
     */
    dr_N[X_AXIS] = *(R_NinE + X_AXIS * 3 + X_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + X_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + X_AXIS) * dr_E[Z_AXIS];
    dr_N[Z_AXIS] = *(R_NinE + X_AXIS * 3 + Z_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + Z_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + Z_AXIS) * dr_E[Z_AXIS];
    dr_N[Y_AXIS] = *(R_NinE + X_AXIS * 3 + Y_AXIS) * dr_E[X_AXIS] +
                   *(R_NinE + Y_AXIS * 3 + Y_AXIS) * dr_E[Y_AXIS] +
                   *(R_NinE + Z_AXIS * 3 + Y_AXIS) * dr_E[Z_AXIS];

    return 1;
}

/** ***************************************************************************
* @name LLA2ECEF Lat [rad], Lon [rad] to Earth Centered Earth Fixed coordinates
*        [m]
* @details pre calculated all non-changing constants
* @param [in] LLA - array [rad] with the Latitude, Lonigtude and altitude
* @param [out] ECEF - array cartresian coordinate [m]
* @retval always 1
******************************************************************************/
BOOL LLA_To_ECEF( double* lla_Rad,
                  double* ecef_m )
{
    double N;
    double cosLat = cos( lla_Rad[LAT] );
    double sinLat = sin( lla_Rad[LAT] );
    double cosLon = cos( lla_Rad[LON] );
    double sinLon = sin( lla_Rad[LON] );

    N = E_MAJOR / sqrt(1.0 - (E_ECC_SQ * sinLat * sinLat)); // radius of Curvature [meters]

    double temp = (N + lla_Rad[ALT]) * cosLat;
    ecef_m[X_AXIS] = temp * cosLon;
    ecef_m[Y_AXIS] = temp * sinLon;
    ecef_m[Z_AXIS] = ((E_MINOR_OVER_MAJOR_SQ * N) + lla_Rad[ALT]) * sinLat;

    return 1;
}


/** ***************************************************************************
* @name Base2ECEF Express tangent (NED) coords in ECEF coordinates
* @details adds delta postion fro start of frame in NED to the ECEF postion
* at the current time step and returns the results in meters ECEF.
* @param [in] NED - input tangent position [m] in North East and Down coords
* @param {in} BaseECEF - start of frame (gps) position [m]
* @param {in} invRne - inverse of the rotation matrix from ECEF to NED
* @param [out] ECEF - current frame position [m]
* @retval always 1
******************************************************************************/
BOOL PosNED_To_PosECEF( real*  r_N,
                        double* rECEF_Init, //BaseECEF,
                        real* R_NinE,
                        double* rECEF)
{
    // ECEF = Base + delta
    *(rECEF + 0) = *(rECEF_Init + 0) + (double)(*(R_NinE + 0 * 3 + 0) * r_N[X_AXIS] +
                                                *(R_NinE + 0 * 3 + 1) * r_N[Y_AXIS] +
                                                *(R_NinE + 0 * 3 + 2) * r_N[Z_AXIS] ); // X
    *(rECEF + 1) = *(rECEF_Init + 1) + (double)(*(R_NinE + 1 * 3 + 0) * r_N[X_AXIS] +
                                                *(R_NinE + 1 * 3 + 1) * r_N[Y_AXIS] +
                                                *(R_NinE + 1 * 3 + 2) * r_N[Z_AXIS] ); // Y
    *(rECEF + 2) = *(rECEF_Init + 2) + (double)(*(R_NinE + 2 * 3 + 0) * r_N[X_AXIS] +
                                                *(R_NinE + 2 * 3 + 1) * r_N[Y_AXIS] +
                                                *(R_NinE + 2 * 3 + 2) * r_N[Z_AXIS] ); // Z

    return 1;
}


/** ***************************************************************************
* @name ECEF2LLA Earth Centered Earth Fixed [m] to Lat [rad], Lon [rad] coordinates
* @details pre calculated all non-changing constants
* @param [out] LLA - array  the Latitude, Lonigtude [deg] and Altitude [m]
* @param [in] ECEF - cartresian coordiante [m]
* @retval always 1
******************************************************************************/
BOOL ECEF_To_LLA(double* llaDeg, double* ecef_m)
{
    double P;
    double theta;
    double sinLat;
    double sinTheta, cosTheta;
    double Lat;

    P = sqrt( ecef_m[X_AXIS] * ecef_m[X_AXIS] + ecef_m[Y_AXIS] * ecef_m[Y_AXIS] );

    //    theta = atan( ( ecef_m[Z_AXIS] * E_MAJOR ) / ( P * E_MINOR ) );   // sqrt( ecef(2) * const )
    theta = atan2(ecef_m[Z_AXIS] * E_MAJOR_OVER_MINOR, P);

    sinTheta = sin(theta);
    cosTheta = cos(theta);

    Lat = atan2((ecef_m[Z_AXIS] + EP_SQ * sinTheta * sinTheta * sinTheta), (P - E_ECC_SQxE_MAJOR * cosTheta * cosTheta * cosTheta));
    *(llaDeg + LAT) = Lat * R2D;
    *(llaDeg + LON) = atan2(ecef_m[Y_AXIS], ecef_m[X_AXIS]) * R2D; // arctan(Y/X)

    sinLat = sin(Lat);
    *(llaDeg + ALT) = P / cos(Lat) - E_MAJOR / sqrt(1.0 - E_ECC_SQ * sinLat * sinLat); // alt

    return 1;
}


/** ***************************************************************************
* @name ECEF2NED Earth Centered Earth Fixed [m] to North [m], East [m] down [m]
* coordinates
* @details Pre calculated all non-changing constants and unfolded the matricies
* @param [in] LLA - Latitude, Longitude and Altitude [rad]
* @param [in] VelECEF - Earth centered earth fixed [m/s]
* @param [out] VelNED - North East Down [m/s]
* @retval always 1
******************************************************************************/
BOOL VelECEF_To_VelNED( double* LLA,
                        real* VelECEF,
                        real* VelNED )
{
    real cosLat, sinLat;
    real cosLon, sinLon;

#if !FAST_MATH
    cosLat = (real)(cos( (real)*(LLA + LAT) ));
    sinLat = (real)(sin( (real)*(LLA + LAT) ));
    cosLon = (real)(cos( (real)*(LLA + LON) ));
    sinLon = (real)(sin( (real)*(LLA + LON) ));
#else
    cosLat = (real)(arm_cos_f32( (real)*(LLA + LAT) ));
    sinLat = (real)(arm_sin_f32( (real)*(LLA + LAT) ));
    cosLon = (real)(arm_cos_f32( (real)*(LLA + LON) ));
    sinLon = (real)(arm_sin_f32( (real)*(LLA + LON) ));
#endif

    // North
    *(VelNED+X_AXIS) = -sinLat * cosLon * *(VelECEF + X_AXIS) +
                           -sinLon * sinLat * *(VelECEF + Y_AXIS) +
                                     cosLat * *(VelECEF + Z_AXIS);
    // East
    *(VelNED+Y_AXIS) =          -sinLon * *(VelECEF + X_AXIS) +
                                     cosLon * *(VelECEF + Y_AXIS);
    // Down
    *(VelNED+Z_AXIS) = -cosLat * cosLon * *(VelECEF + X_AXIS) +
                           -cosLat * sinLon * *(VelECEF + Y_AXIS) +
                                    -sinLat * *(VelECEF + Z_AXIS);

    return 1;
}

void printMtx(float *a, int m, int n)
{
    int i, j;
    for (i = 0; i < m; i++)
    {
        for (j = 0; j < n - 1; j++)
        {
            printf("%.9g, ", a[i*n + j]);
        }
        printf("%.9g\n", a[i*n + j]);
    }
}

void printVec(float *v, int n)
{
    int i;
    for (i = 0; i < n - 1; i++)
    {
        printf("%.9g, ", v[i]);
    }
    printf("%.9g\n", v[i]);
}

real AngleErrDeg(real aErr)
{
    while (fabs(aErr) > 180.0)
    {
        if (aErr > 180.0)
        {
            aErr -= (real)360.0;
        }
        else if (aErr < -180.0)
        {
            aErr += (real)360.0;
        }
    }

    return aErr;
}

int realSymmetricMtxEig(float *a, int n, float *v, float eps, int jt)
{
    int i, j, p, q, u, w, t, s, l;
    double fm, cn, sn, omega, x, y, d;
    l = 1;
    p = 0; q = 0;   // initialized AB
    for (i = 0; i <= n - 1; i++)
    {
        v[i*n + i] = 1.0;
        for (j = 0; j <= n - 1; j++)
        {
            if (i != j)
            {
                v[i*n + j] = 0.0;
            }
        }
    }
    while (1)
    {
        fm = 0.0;
        for (i = 0; i <= n - 1; i++)
        {
            for (j = 0; j <= n - 1; j++)
            {
                d = fabs(a[i*n + j]);
                if ((i != j) && (d > fm))
                {
                    fm = d;
                    p = i;
                    q = j;
                }
            }
        }
        if (fm < eps)
        {
            return(1);
        }
        if (l > jt)
        {
            return(-1);
        }
        l = l + 1;
        u = p * n + q;
        w = p * n + p;
        t = q * n + p;
        s = q * n + q;
        x = -a[u];
        y = (a[s] - a[w]) / 2.0;
        omega = x / sqrt(x*x + y * y);
        if (y < 0.0)
        {
            omega = -omega;
        }
        sn = 1.0 + sqrt(1.0 - omega * omega);
        sn = omega / sqrt(2.0*sn);
        cn = sqrt(1.0 - sn * sn);
        fm = a[w];
        a[w] = fm * cn*cn + a[s] * sn*sn + a[u] * omega;
        a[s] = fm * sn*sn + a[s] * cn*cn - a[u] * omega;
        a[u] = 0.0;
        a[t] = 0.0;
        for (j = 0; j <= n - 1; j++)
        {
            if ((j != p) && (j != q))
            {
                u = p * n + j;
                w = q * n + j;
                fm = a[u];
                a[u] = fm * cn + a[w] * sn;
                a[w] = -fm * sn + a[w] * cn;
            }
        }
        for (i = 0; i <= n - 1; i++)
        {
            if ((i != p) && (i != q))
            {
                u = i * n + p;
                w = i * n + q;
                fm = a[u];
                a[u] = fm * cn + a[w] * sn;
                a[w] = -fm * sn + a[w] * cn;
            }
        }
        for (i = 0; i <= n - 1; i++)
        {
            u = i * n + p;
            w = i * n + q;
            fm = v[u];
            v[u] = fm * cn + v[w] * sn;
            v[w] = -fm * sn + v[w] * cn;
        }
    }
    return(1);
}