
#include "imu.h"

void quatConjugate(quaternion_fix *q, quaternion_fix *result) {
    result->q0 = q->q0;
    result->q1 = -q->q1;
    result->q2 = -q->q2;
    result->q3 = -q->q3;
}

void quatProduct(quaternion_fix *q1, quaternion_fix *q2, quaternion_fix *result) {
    result->q0 =
        q16_mul(q1->q0, q2->q0) -
        q16_mul(q1->q1, q2->q1) -
        q16_mul(q1->q2, q2->q2) -
        q16_mul(q1->q3, q2->q3);
    result->q1 =
        q16_mul(q1->q0, q2->q1) +
        q16_mul(q1->q1, q2->q0) +
        q16_mul(q1->q2, q2->q3) -
        q16_mul(q1->q3, q2->q2);
    result->q2 =
        q16_mul(q1->q0, q2->q2) -
        q16_mul(q1->q1, q2->q3) +
        q16_mul(q1->q2, q2->q0) +
        q16_mul(q1->q3, q2->q1);
    result->q3 =
        q16_mul(q1->q0, q2->q3) +
        q16_mul(q1->q1, q2->q2) -
        q16_mul(q1->q2, q2->q1) +
        q16_mul(q1->q3, q2->q0);
}

void quatRotate(quaternion_fix *q, quaternion_fix *v, quaternion_fix *result) {
    quaternion_fix qconj, tmp;
    quatConjugate(q, &qconj);
    quatProduct(v, &qconj, &tmp);
    quatProduct(q, &tmp, result);
}

void linearAcceleration(quaternion_fix * orientation, int16_t *rawAccel,
    int16_t *linearAccel) {
    quaternion_fix qRawAccel, qWorldAccel;

    // Rotate the acceleration vector to the world frame
    qRawAccel.q0 = 0;
    qRawAccel.q1 = rawAccel[0];
    qRawAccel.q2 = rawAccel[1];
    qRawAccel.q3 = rawAccel[2];
    quatRotate(orientation, &qRawAccel, &qWorldAccel);

    // Return the resulting acceleration, canceling out gravity
    linearAccel[0] = qWorldAccel.q1;
    linearAccel[1] = qWorldAccel.q2;
    linearAccel[2] = qWorldAccel.q3 - 16383;
}

void linearAccelerationXYProjection(quaternion_fix * q, int16_t * linearAccel,
    int16_t *xyprojection) {
    q16 q0q0 = q16_mul(q->q0, q->q0),
        q1q1 = q16_mul(q->q1, q->q1),
        q2q2 = q16_mul(q->q2, q->q2),
        q3q3 = q16_mul(q->q3, q->q3),
        q1q2 = q16_mul(q->q1, q->q2),
        q0q3 = q16_mul(q->q0, q->q3);

    q16 sensorX_x = q0q0 + q1q1 - q2q2 - q3q3;
    q16 sensorX_y = 2 * (q1q2 + q0q3);
    q16 sensorY_x = 2 * (q1q2 - q0q3);
    q16 sensorY_y = q0q0 - q1q1 + q2q2 - q3q3;

    xyprojection[0] = q16_mul(sensorX_x, linearAccel[0]) +
        q16_mul(sensorX_y, linearAccel[1]);
    xyprojection[1] = q16_mul(sensorY_x, linearAccel[0]) +
        q16_mul(sensorY_y, linearAccel[1]);
}

q16 sinRoll(quaternion_fix * const quat) {
    return 2 * (q16_mul(quat->q0, quat->q1) + q16_mul(quat->q2, quat->q3));
}

q16 sinPitch(quaternion_fix * const quat) {
    return 2 * (q16_mul(quat->q1, quat->q3) - q16_mul(quat->q0, quat->q2));
}

//-----------------------------------------------------------------------------
// IMU algorithm update

//=============================================================================
// originally from MadgwickAHRS.c
//=============================================================================
//
// Implementation of Madgwick's IMU and AHRS algorithms.
// See: http://www.x-io.co.uk/node/8#open_source_ahrs_and_imu_algorithms
//
// Date	        Author          Notes
// 29/09/2011   SOH Madgwick    Initial release
// 02/10/2011   SOH Madgwick    Optimised for reduced CPU load
// 19/02/2012   SOH Madgwick    Magnetometer measurement is normalised
// 14/12/2016   Sakari Kapanen  Fixed point version of the algorithm
//
//=============================================================================
void MadgwickAHRSupdateIMU_fix(q16 beta, q16 gyroIntegrationFactor,
    int16_t *rawAccel, int16_t *rawGyro, quaternion_fix * const q) {
    q16 recipNorm;
    q16 q0, q1, q2, q3;
    q16 s0, s1, s2, s3;
    q16 qDot0, qDot1, qDot2, qDot3;
    q16 _2q1q1q2q2, tmpterm;

    q0 = q->q0;
    q1 = q->q1;
    q2 = q->q2;
    q3 = q->q3;

    q16 gx = rawGyro[0], gy = rawGyro[1], gz = rawGyro[2];

    // Rate of change of quaternion from gyroscope
    // NOTE these values are actually double the actual values but it
    // does not matter
    qDot0 = -q16_mul(q1, gx) - q16_mul(q2, gy) - q16_mul(q3, gz);
    qDot1 = q16_mul(q0, gx) + q16_mul(q2, gz) - q16_mul(q3, gy);
    qDot2 = q16_mul(q0, gy) - q16_mul(q1, gz) + q16_mul(q3, gx);
    qDot3 = q16_mul(q0, gz) + q16_mul(q1, gy) - q16_mul(q2, gx);

    q16 ax = rawAccel[0], ay = rawAccel[1], az = rawAccel[2];
    // Compute feedback only if accelerometer measurement valid
    // (avoids NaN in accelerometer normalisation)
    if (ax != 0 || ay != 0 || az != 0) {
        // Normalise accelerometer measurement
        recipNorm = q16_mul(ax, ax);
        recipNorm += q16_mul(ay, ay);
        recipNorm += q16_mul(az, az);
        recipNorm = q16_rsqrt(recipNorm);
        ax = q16_mul(ax, recipNorm);
        ay = q16_mul(ay, recipNorm);
        az = q16_mul(az, recipNorm);

        // Auxiliary variables to avoid repeated arithmetic
        _2q1q1q2q2 = q16_mul(q1, q1) + q16_mul(q2, q2);
        tmpterm = az + _2q1q1q2q2;
        tmpterm += tmpterm;
        _2q1q1q2q2 += _2q1q1q2q2;

        // Gradient descent algorithm corrective step
        s0 = q16_mul(q0, _2q1q1q2q2) + q16_mul(q2, ax) - q16_mul(q1, ay);
        s1 = q16_mul(q1, tmpterm) - q16_mul(q3, ax) - q16_mul(q0, ay);
        s2 = q16_mul(q2, tmpterm) + q16_mul(q0, ax) - q16_mul(q3, ay);
        s3 = q16_mul(q3, _2q1q1q2q2) - q16_mul(q1, ax) - q16_mul(q2, ay);

        // Apply feedback step
        recipNorm = q16_mul(s0, s0);
        recipNorm += q16_mul(s1, s1);
        recipNorm += q16_mul(s2, s2);
        recipNorm += q16_mul(s3, s3);
        recipNorm = q16_rsqrt(recipNorm);
        recipNorm = q16_mul(beta, recipNorm);
        qDot0 -= q16_mul(recipNorm, s0);
        qDot1 -= q16_mul(recipNorm, s1);
        qDot2 -= q16_mul(recipNorm, s2);
        qDot3 -= q16_mul(recipNorm, s3);
    }

    // Integrate rate of change of quaternion to yield quaternion
    q0 += q16_mul(qDot0, gyroIntegrationFactor);
    q1 += q16_mul(qDot1, gyroIntegrationFactor);
    q2 += q16_mul(qDot2, gyroIntegrationFactor);
    q3 += q16_mul(qDot3, gyroIntegrationFactor);

    // Normalise quaternion
    recipNorm = q16_mul(q0, q0);
    recipNorm += q16_mul(q1, q1);
    recipNorm += q16_mul(q2, q2);
    recipNorm += q16_mul(q3, q3);
    recipNorm = q16_rsqrt(recipNorm);
    q->q0 = q16_mul(q0, recipNorm);
    q->q1 = q16_mul(q1, recipNorm);
    q->q2 = q16_mul(q2, recipNorm);
    q->q3 = q16_mul(q3, recipNorm);
}
