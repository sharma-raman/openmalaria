/*
 This file is part of OpenMalaria.
 
 Copyright (C) 2005-2010 Swiss Tropical Institute and Liverpool School Of Tropical Medicine
 
 OpenMalaria is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef Hmod_AgeGroupInterpolationSuite
#define Hmod_AgeGroupInterpolationSuite

#include <cxxtest/TestSuite.h>
#include "UnittestUtil.h"
#include "ExtraAsserts.h"
#include "util/AgeGroupInterpolation.h"

using util::AgeGroupInterpolation;

class AgeGroupInterpolationSuite : public CxxTest::TestSuite
{
public:
    AgeGroupInterpolationSuite () {
        scnXml::AgeGroupValues::GroupSequence& seq = agvElt.getGroup();
        seq.resize( dataLen, scnXml::Group::Group(0.0,0.0) );
        for( size_t i = 0; i < dataLen; ++i ){
            seq[ i ].setLowerbound( stdLbounds[ i ] );
            seq[ i ].setValue( stdValues[ i ] );
        }
    }
    ~AgeGroupInterpolationSuite () {
    }
    
    void setUp () {
    }
    void tearDown () {
    }
    
    void testDummy() {
        AgeGroupInterpolation *o = AgeGroupInterpolation::dummyObject();
        TS_ASSERT_THROWS( (*o)(5.7), const std::logic_error &e );
        AgeGroupInterpolation::freeObject(o);
    }
    
    void testPiecewiseConst () {
        agvElt.setInterpolation( "none" );
        AgeGroupInterpolation *o = AgeGroupInterpolation::makeObject( agvElt, "testPiecewiseConst" );
        for( size_t i = 0; i < testLen; ++i ){
            TS_ASSERT_APPROX( (*o)( testAges[ i ] ), piecewiseConstValues[ i ] );
        }
        AgeGroupInterpolation::freeObject(o);
    }
    
    void testLinearInterp () {
        agvElt.setInterpolation( "linear" );
        AgeGroupInterpolation *o = AgeGroupInterpolation::makeObject( agvElt, "testLinearInterp" );
        for( size_t i = 0; i < testLen; ++i ){
            TS_ASSERT_APPROX( (*o)( testAges[ i ] ), linearInterpValues[ i ] );
        }
        AgeGroupInterpolation::freeObject(o);
    }
    
private:
    static const size_t dataLen = 18;
    static const size_t testLen = 7;
    static const double stdLbounds[dataLen];
    static const double stdValues[dataLen];
    static const double testAges[testLen];
    static const double piecewiseConstValues[testLen];
    static const double linearInterpValues[testLen];
    scnXml::AgeGroupValues agvElt;
};

const double AgeGroupInterpolationSuite::stdLbounds[] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,20,20
};
const double AgeGroupInterpolationSuite::stdValues[] = {
    0.225940909648,0.286173633441,0.336898395722,0.370989854675,
    0.403114915112,0.442585112522,0.473839351511,0.512630464378,
    0.54487872702,0.581527755812,0.630257580698,0.663063362714,
    0.702417432755,0.734605377277,0.788908765653,0.839587932303,
    1.0,1.0
};
const double AgeGroupInterpolationSuite::testAges[] = {
    // various ages, designed to test limits, boundary points and interpolation
    15.2,18.09,7.0, 2.5,
    0.0,20.0,900.0
};
const double AgeGroupInterpolationSuite::piecewiseConstValues[] = {
    0.839587932303, 0.839587932303, 0.51263046437799997, 0.33689839572199998,
    0.22594090964800001, 1.0, 1.0
};
const double AgeGroupInterpolationSuite::linearInterpValues[] = {
    0.87701741476563333, 1.0, 0.52875459569899996, 0.37098985467500001,
    0.22594090964800001, 1.0, 1.0
};

#endif