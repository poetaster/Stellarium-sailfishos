/*
 * Stellarium
 * Copyright (C) 2013 Guillaume Chereau
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
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */

#ifndef Q_OS_IOS

#include "SensorsMgr.hpp"
#include "StelTranslator.hpp"
#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelModuleMgr.hpp"
#include "StelMovementMgr.hpp"
#include "StelUtils.hpp"
#include "StelFileMgr.hpp"
#ifdef Q_OS_ANDROID
#  include "StelAndroid.hpp"
#endif
#include <GeographicLib/MagneticModel.hpp>

#include <QDebug>
#include <QAccelerometer>
#include <QMagnetometer>
#include <QScreen>
#include <QGuiApplication>
#include <QOrientationSensor>
#include <QOrientationReading>
#include <QFile>

SensorsMgr::SensorsMgr() :
    enabled(false),
    accelerometerSensor(NULL),
    magnetometerSensor(NULL),
    sensorX(0), sensorY(0), sensorZ(0),
    magnetX(0), magnetY(0), magnetZ(0),
    mOrientation(0),
    firstMeasure(true)
{
	setObjectName("SensorsMgr");
}

SensorsMgr::~SensorsMgr()
{
	
}

void SensorsMgr::init()
{
	addAction("actionSensorsControl", N_("Movement and Selection"), N_("Sensors"), "enabled");
	accelerometerSensor = new QAccelerometer(this);
	// Crash with Qt 5.3.
	// accelerometerSensor->setAccelerationMode(QAccelerometer::Gravity);
	magnetometerSensor = new QMagnetometer(this);
	orientationSensor = new QOrientationSensor(this);
	connect(orientationSensor, &QOrientationSensor::readingChanged,
            [=]() {
                int newOrientation = mOrientation;
                switch (orientationSensor->reading()->orientation()) {
                        case QOrientationReading::Orientation::TopUp:
                            newOrientation = 0;
                            break;

                        case QOrientationReading::Orientation::LeftUp:
                            newOrientation = 90;
                            break;

                        case QOrientationReading::Orientation::TopDown:
                            newOrientation = 180;
                            break;

                        case QOrientationReading::Orientation::RightUp:
                            newOrientation = 270;
                            break;

                        default:
                            break;
                    }
                if (newOrientation != mOrientation) {
                    mOrientation = newOrientation;
                }
                qDebug() << "orientationSensor:" << mOrientation;

        }
        );
}

void SensorsMgr::setEnabled(bool value)
{
	if (value == enabled)
		return;
	enabled = value;
	accelerometerSensor->setActive(enabled);
    magnetometerSensor->setActive(enabled);
    orientationSensor->setActive(enabled);

	firstMeasure = true;
	if (!enabled)
	{
		Vec3d up(0,0,1);
		StelMovementMgr* mmgr = GETSTELMODULE(StelMovementMgr);
		mmgr->setViewUpVectorJ2000(StelApp::getInstance().getCore()->altAzToJ2000(up));
		StelApp::getInstance().getCore()->setDefautAngleForGravityText(0);
    }
    // Begin modification: add magnetic declination correction (Cheng Xinlun, Apr 18, 2017)
    if (enabled)
    {
        const StelLocation location = StelApp::getInstance().getCore()->getCurrentLocation();
        double longitude = location.longitude;
        double latitude = location.latitude;
        double height = location.altitude;
        double t = StelUtils::jdToQDateTime(StelApp::getInstance().getCore()->getJDay()).date().year();
        try
        {
            QString wmm_dir = StelFileMgr::findFile("data/magnetic/wmm2015.wmm");
            QString cof_dir = StelFileMgr::findFile("data/magnetic/wmm2015.wmm.cof");
            // Cheat to get absolute dir
            QFile wmm_file(wmm_dir);
            wmm_file.copy(StelFileMgr::getUserDir() + "wmm2015.wmm");
            QFile cof_file(cof_dir);
            cof_file.copy(StelFileMgr::getUserDir() + "wmm2015.wmm.cof");
            GeographicLib::MagneticModel* mag = new GeographicLib::MagneticModel("wmm2015", StelFileMgr::getUserDir().toStdString());
            double Bx, By, Bz;
            (*mag)(t, latitude, longitude, height, Bx, By, Bz);
            double h, f, i;
            GeographicLib::MagneticModel::FieldComponents(Bx, By, Bz, h, f, magd, i);
            delete mag;
            wmm_file.remove();
            cof_file.remove();
            qDebug() << "Magnetic declination: " << magd;
        }
        catch (const std::runtime_error& e)
        {
            qWarning() << "RuntimeError in GeographicLib: " << e.what();
            qWarning() << "Magnetic declination correction will not funcion correctly.";
            magd = 0.0;
        }
	}
    // End modification
	emit enabledChanged(enabled);
}

static qreal mix(qreal x, qreal y, qreal t)
{
	return x * (1 - t) + y * t;
}

static void rot2d(float* x, float* y, float a)
{
	float cs = cos(a);
	float sn = sin(a);
	float x2 = *x * cs - *y * sn;
	float y2 = *x * sn + *y * cs;
	*x = x2;
	*y = y2;
}

// Note: QScreen.orientation() does nto work on android when we go from 0deg to 180deg.
// That is why we have to do a JNI call to get the real orientation.
#ifdef defined(Q_OS_ANDROID)
void SensorsMgr::applyOrientation(float* x, float* y, float* z)
{
	Q_UNUSED(z);
	const float xx = *x, yy = *y;
	switch (StelAndroid::getOrientation())
	{
	case 0:  // ROTATION_0
		break;
	case 1:  // ROTATION_90
		*x = -yy;
		*y = xx;
		break;
	case 2:  // ROTATION_180
		*x = -xx;
		*y = -yy;
		break;
	case 3:  // ROTATION_270
		*x = yy;
		*y = -xx;
		break;
	}
}
#elif defined(Q_OS_UBUNTU_TOUCH)
void SensorsMgr::applyOrientation(float* x, float* y, float* z)
{

   Q_UNUSED(z);
   	const float xx = *x, yy = *y;
   	//qDebug() << "x,y,pos" << xx << yy << mOrientation;
   	switch (mOrientation)
   	{
   	case 0:
   		break;
   	case 90:
   		*x = yy;
        *y = -xx;
        break;
   	case 180:
   		*x = -xx;
   		*y = -yy;
   		break;
   	case 270:
   	    *x = -yy;
        *y = xx;
        break;
   	}
}
#else
void SensorsMgr::applyOrientation(float* x, float *y, float* z) 
{
	Q_UNUSED(x); Q_UNUSED(y); Q_UNUSED(z);
}
#endif

void SensorsMgr::update(double deltaTime)
{
	Q_UNUSED(deltaTime);
	if (!enabled)
		return;
	QAccelerometerReading* reading = accelerometerSensor->reading();
	if (!reading)
		return;


	float fov = StelApp::getInstance().getCore()->getProjection(StelCore::FrameJ2000)->getFov();
	float averagingCoef = (firstMeasure)? 1 : mix(0.01, 0.1, qMin(fov / 130.0, 1.0));
	firstMeasure = false;
	const qreal g = 9.80665;
	sensorX = mix(sensorX, reading->x() / g, averagingCoef);
	sensorY = mix(sensorY, reading->y() / g, averagingCoef);
	sensorZ = mix(sensorZ, reading->z() / g, averagingCoef);

	float x = sensorX;
	float y = sensorY;
	float z = sensorZ;
	applyOrientation(&x, &y, &z);

	float roll = std::atan2(-x, y);
	float pitch = std::atan2(-z, std::sqrt(x*x + y*y));

	StelApp::getInstance().getCore()->setDefautAngleForGravityText(roll*180./M_PI);
	StelMovementMgr* mmgr = GETSTELMODULE(StelMovementMgr);
	Vec3d viewDirection = StelApp::getInstance().getCore()->j2000ToAltAz(mmgr->getViewDirectionJ2000());

	float lng, lat;
	StelUtils::rectToSphe(&lng, &lat, viewDirection);
	StelUtils::spheToRect(lng, pitch, viewDirection);
	mmgr->setViewDirectionJ2000(StelApp::getInstance().getCore()->altAzToJ2000(viewDirection));

	Vec3f viewHoriz;
	StelUtils::spheToRect(lng, 0, viewHoriz);
	Mat4f rot=Mat4f::rotation(viewHoriz, roll);
	Vec3f up(0,0,1);
	up.transfo4d(rot);
	Vec3d tmp(up[0],up[1],up[2]);
	mmgr->setViewUpVectorJ2000(StelApp::getInstance().getCore()->altAzToJ2000(tmp));
	Mat4f mat = Mat4f::identity();
	mat = Mat4f::rotation(Vec3f(1, 0, 0), pitch) * mat;

    QMagnetometerReading* magnetoReading = magnetometerSensor->reading();
	if (!magnetoReading)
            return;
    magnetX = mix(magnetX, magnetoReading->x(), averagingCoef);
    magnetY = mix(magnetY, magnetoReading->y(), averagingCoef);
    magnetZ = mix(magnetZ, magnetoReading->z(), averagingCoef);
	x = magnetX;
	y = magnetY;
	z = magnetZ;
    applyOrientation(&x, &y, &z);

    rot2d(&x, &y, -roll);
    rot2d(&y, &z, pitch);
    float az = std::atan2(-x, z) - magd * 0.0174533; // Magnetic declination correction (Cheng Xinlun, Apr 18, 2017)
    StelUtils::spheToRect(az, pitch, viewDirection);
    mmgr->setViewDirectionJ2000(StelApp::getInstance().getCore()->altAzToJ2000(viewDirection));
}

#endif // Q_OS_IOS
