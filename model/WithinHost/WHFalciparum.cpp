/* This file is part of OpenMalaria.
 *
 * Copyright (C) 2005-2014 Swiss Tropical and Public Health Institute
 * Copyright (C) 2005-2014 Liverpool School Of Tropical Medicine
 *
 * OpenMalaria is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "WithinHost/WHFalciparum.h"
#include "WithinHost/DescriptiveWithinHost.h"
#include "WithinHost/CommonWithinHost.h"
#include "WithinHost/Infection/DummyInfection.h"
#include "WithinHost/Infection/EmpiricalInfection.h"
#include "WithinHost/Infection/MolineauxInfection.h"
#include "WithinHost/Infection/PennyInfection.h"
#include "WithinHost/Pathogenesis/PathogenesisModel.h"
#include "WithinHost/Diagnostic.h"
#include "WithinHost/Treatments.h"
#include "util/random.h"
#include "util/ModelOptions.h"
#include "util/errors.h"
#include "util/StreamValidator.h"
#include "util/checkpoint_containers.h"
#include "schema/scenario.h"

#include <cmath>
#include <boost/format.hpp>
#include <gsl/gsl_cdf.h>


namespace OM {
namespace WithinHost {

using namespace OM::util;
using namespace Monitoring;

double WHFalciparum::sigma_i;
double WHFalciparum::immPenalty_22;
double WHFalciparum::asexImmRemain;
double WHFalciparum::immEffectorRemain;
int WHFalciparum::y_lag_len = 0;

// -----  static functions  -----

void WHFalciparum::init( const OM::Parameters& parameters, const scnXml::Scenario& scenario ) {
    sigma_i=sqrt(parameters[Parameters::SIGMA_I_SQ]);
    immPenalty_22=1-exp(parameters[Parameters::IMMUNITY_PENALTY]);
    immEffectorRemain=exp(-parameters[Parameters::IMMUNE_EFFECTOR_DECAY]);
    asexImmRemain=exp(-parameters[Parameters::ASEXUAL_IMMUNITY_DECAY]);
    
    y_lag_len = sim::daysToSteps(20);
    
    //NOTE: should also call cleanup() on the PathogenesisModel, but it only frees memory which the OS does anyway
    Pathogenesis::PathogenesisModel::init( parameters, scenario.getModel().getClinical(), false );
    
    /*
    The detection limit (in parasites/ul) is currently the same for PCR and for microscopy
    TODO: in fact the detection limit in Garki should be the same as the PCR detection limit
    The density bias allows the detection limit for microscopy to be higher for other sites
    */
    double densitybias;
    if (util::ModelOptions::option (util::GARKI_DENSITY_BIAS)) {
        densitybias = parameters[Parameters::DENSITY_BIAS_GARKI];
    } else {
        if( scenario.getAnalysisNo().present() ){
            int analysisNo = scenario.getAnalysisNo().get();
            if ((analysisNo >= 22) && (analysisNo <= 30)) {
                cerr << "Warning: these analysis numbers used to mean use Garki density bias. If you do want to use this, specify the option GARKI_DENSITY_BIAS; if not, nothing's wrong." << endl;
            }
        }
        densitybias = parameters[Parameters::DENSITY_BIAS_NON_GARKI];
    }
    double detectionLimit=scenario.getMonitoring().getSurveys().getDetectionLimit()*densitybias;
    Diagnostic::default_.setDeterministic( detectionLimit );
    
    //FIXME(schema): input should be in days
    SimTime latentP = sim::fromTS(scenario.getModel().getParameters().getLatentp());
    Infection::init( parameters, latentP );
}


// -----  Non-static  -----

WHFalciparum::WHFalciparum( double comorbidityFactor ):
    WHInterface(),
    m_cumulative_h(0.0), m_cumulative_Y(0.0), m_cumulative_Y_lag(0.0),
    totalDensity(0.0), timeStepMaxDensity(0.0),
    pathogenesisModel( Pathogenesis::PathogenesisModel::createPathogenesisModel( comorbidityFactor ) )
{
    // NOTE: negating a Gaussian sample with mean 0 is pointless — except that
    // the individual samples change. In any case the overhead is negligible.
    //FIXME: Should this be allowed to be greater than 1?
    // Oldest code on GoogleCode: _innateImmunity=(double)(W_GAUSS((0), (sigma_i)));
    _innateImmSurvFact = exp(-random::gauss(sigma_i));
    
    m_y_lag.assign (y_lag_len, 0.0);
}

WHFalciparum::~WHFalciparum()
{
}

double WHFalciparum::probTransmissionToMosquito( SimTime ageOfHuman, double tbvFactor ) const{
    /* This model (often referred to as the gametocyte model) was designed for
    5-day time steps. We use the same model (sampling 10, 15 and 20 days ago)
    for 1-day time steps to avoid having to design and analyse a new model.
    Description: AJTMH pp.32-33 */
    
    /* Note: we don't allow for treatment which clears gametocytes (e.g.
     * Primaquine). Apparently Primaquine is not commonly used in P falciparum
     * treatment, but for vivax the effect may be important. */
    
    //NOTE: this seems totally pointless to me. If m_y_lag is initialised to zero
    // then calculations should work correctly anyway.
    if (ageOfHuman.inDays() <= 20 || sim::now1().inDays() <= 20){
        // We need at least 20 days history (m_y_lag) to calculate infectiousness;
        // assume no infectiousness if we don't have this history.
        // Note: human not updated on DOB so age must be >20 days.
        return 0.0;
    }
    
    //Infectiousness parameters: see AJTMH p.33, tau=1/sigmag**2 
    static const double beta1=1.0;
    static const double beta2=0.46;
    static const double beta3=0.17;
    static const double tau= 0.066;
    static const double mu= -8.1;
    
    // Take weighted sum of total asexual blood stage density 10, 15 and 20 days
    // before. We have 20 days history, so use mod_nn:
    int firstIndex = sim::daysToSteps(sim::now1().inDays() - 10) + 1;
    double x = beta1 * m_y_lag[mod_nn(firstIndex, y_lag_len)]
            + beta2 * m_y_lag[mod_nn(firstIndex - sim::daysToSteps(5), y_lag_len)]
            + beta3 * m_y_lag[mod_nn(firstIndex - sim::daysToSteps(10), y_lag_len)];
    if (x < 0.001){
        return 0.0;
    }
    
    double zval=(log(x)+mu)/sqrt(1.0/tau);
    double pone = gsl_cdf_ugaussian_P(zval);
    double transmit=(pone*pone);
    //transmit has to be between 0 and 1
    transmit=std::max(transmit, 0.0);
    transmit=std::min(transmit, 1.0);
    
    //    Include here the effect of transmission-blocking vaccination
    double result = transmit * tbvFactor;
    util::streamValidate( result );
    return result;
}

bool WHFalciparum::diagnosticDefault() const{
    return Diagnostic::default_.isPositive( totalDensity );
}

void WHFalciparum::treatment( Host::Human& human, TreatmentId treatId ){
    const Treatments& treat = Treatments::select( treatId );
    if( treat.liverEffect() != sim::zero() ){
        if( treat.liverEffect() < sim::zero() )
            clearInfections( Treatments::LIVER );
        else
            treatExpiryLiver = max( treatExpiryLiver, sim::now1() + treat.liverEffect() );
    }
    if( treat.bloodEffect() != sim::zero() ){
        if( treat.bloodEffect() < sim::zero() )
            clearInfections( Treatments::BLOOD );
        else
            treatExpiryBlood = max( treatExpiryBlood, sim::now1() + treat.bloodEffect() );
    }
    
    // triggered intervention deployments:
    treat.deploy( human,
                  interventions::Deployment::TREAT,
                  interventions::VaccineLimits(/*default initialise: no limits*/) );
}
void WHFalciparum::treatSimple(SimTime timeLiver, SimTime timeBlood){
    if( timeLiver != sim::zero() ){
        if( timeLiver < sim::zero() )
            clearInfections( Treatments::LIVER );
        else
            treatExpiryLiver = max( treatExpiryLiver, sim::now1() + timeLiver );
    }
    if( timeBlood != sim::zero() ){
        if( timeBlood < sim::zero() )
            clearInfections( Treatments::BLOOD );
        else
            treatExpiryBlood = max( treatExpiryBlood, sim::now1() + timeBlood );
    }
}

Pathogenesis::StatePair WHFalciparum::determineMorbidity(double ageYears){
    Pathogenesis::StatePair result =
            pathogenesisModel->determineState( ageYears, timeStepMaxDensity, totalDensity );
    
    /* Note: this model can easily be re-enabled, but is not used and not considered to be a good model.
    if( (result.state & Pathogenesis::MALARIA) && util::ModelOptions::option( util::PENALISATION_EPISODES ) ){
        // This does immunity penalisation:
        m_cumulative_Y = m_cumulative_Y_lag - immPenalty_22*(m_cumulative_Y-m_cumulative_Y_lag);
        if (m_cumulative_Y < 0) {
            m_cumulative_Y=0.0;
        }
    }*/
    
    return result;
}


// -----  immunity  -----

void WHFalciparum::updateImmuneStatus() {
    if (immEffectorRemain < 1) {
        m_cumulative_h*=immEffectorRemain;
        m_cumulative_Y*=immEffectorRemain;
    }
    if (asexImmRemain < 1) {
        m_cumulative_h*=asexImmRemain/
                      (1+(m_cumulative_h*(1-asexImmRemain) * Infection::invCumulativeHstar));
        m_cumulative_Y*=asexImmRemain/
                      (1+(m_cumulative_Y*(1-asexImmRemain) * Infection::invCumulativeYstar));
    }
    m_cumulative_Y_lag = m_cumulative_Y;
}


// -----  Summarize  -----

bool WHFalciparum::summarize (const Host::Human& human) {
    Survey& survey = Survey::current();
    pathogenesisModel->summarize( human );
    InfectionCount count = countInfections();
    if (count.total != 0) {
        survey
            .addInt( Report::MI_INFECTED_HOSTS, human, 1 )
            .addInt( Report::MI_INFECTIONS, human, count.total )
            .addInt( Report::MI_PATENT_INFECTIONS, human, count.patent );
    }
    // Treatments in the old ImmediateOutcomes clinical model clear infections immediately
    // (and are applied after update()); here we report the last calculated density.
    if (diagnosticDefault()) {
        survey
            .addInt( Report::MI_PATENT_HOSTS, human, 1)
            .addDouble( Report::MD_LOG_DENSITY, human, log(totalDensity) );
        return true;
    }
    return false;
}


void WHFalciparum::checkpoint (istream& stream) {
    WHInterface::checkpoint( stream );
    _innateImmSurvFact & stream;
    m_cumulative_h & stream;
    m_cumulative_Y & stream;
    m_cumulative_Y_lag & stream;
    totalDensity & stream;
    timeStepMaxDensity & stream;
    m_y_lag & stream;
    (*pathogenesisModel) & stream;
    treatExpiryLiver & stream;
    treatExpiryBlood & stream;
}
void WHFalciparum::checkpoint (ostream& stream) {
    WHInterface::checkpoint( stream );
    _innateImmSurvFact & stream;
    m_cumulative_h & stream;
    m_cumulative_Y & stream;
    m_cumulative_Y_lag & stream;
    totalDensity & stream;
    timeStepMaxDensity & stream;
    m_y_lag & stream;
    (*pathogenesisModel) & stream;
    treatExpiryLiver & stream;
    treatExpiryBlood & stream;
}

}
}
