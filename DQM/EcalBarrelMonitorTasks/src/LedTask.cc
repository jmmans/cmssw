#include "../interface/LedTask.h"

#include "CalibCalorimetry/EcalLaserAnalyzer/interface/MEEBGeom.h"
#include "CalibCalorimetry/EcalLaserAnalyzer/interface/MEEEGeom.h"

#include "DQM/EcalCommon/interface/EcalDQMCommonUtils.h"

namespace ecaldqm {

  LedTask::LedTask(const edm::ParameterSet &_params, const edm::ParameterSet& _paths) :
    DQWorkerTask(_params, _paths, "LedTask"),
    ledWavelengths_(),
    MGPAGainsPN_(),
    pnAmp_()
  {
    using namespace std;

    collectionMask_ = 
      (0x1 << kEcalRawData) |
      (0x1 << kEEDigi) |
      (0x1 << kPnDiodeDigi) |
      (0x1 << kEBUncalibRecHit) |
      (0x1 << kEEUncalibRecHit);

    edm::ParameterSet const& commonParams(_params.getUntrackedParameterSet("Common"));
    MGPAGainsPN_ = commonParams.getUntrackedParameter<std::vector<int> >("MGPAGainsPN");

    edm::ParameterSet const& taskParams(_params.getUntrackedParameterSet(name_));
    ledWavelengths_ = taskParams.getUntrackedParameter<std::vector<int> >("ledWavelengths");

    for(std::vector<int>::iterator wlItr(ledWavelengths_.begin()); wlItr != ledWavelengths_.end(); ++wlItr)
      if(*wlItr != 1 && *wlItr != 2) throw cms::Exception("InvalidConfiguration") << "Led Wavelength" << std::endl;

    for(std::vector<int>::iterator gainItr(MGPAGainsPN_.begin()); gainItr != MGPAGainsPN_.end(); ++gainItr)
      if(*gainItr != 1 && *gainItr != 16) throw cms::Exception("InvalidConfiguration") << "PN diode gain" << std::endl;	

    map<string, string> replacements;
    stringstream ss;

    for(vector<int>::iterator wlItr(ledWavelengths_.begin()); wlItr != ledWavelengths_.end(); ++wlItr){
      ss.str("");
      ss << *wlItr;
      replacements["wl"] = ss.str();

      unsigned offset(*wlItr - 1);

      MEs_[kAmplitudeSummary + offset]->name(replacements);
      MEs_[kAmplitude + offset]->name(replacements);
      MEs_[kOccupancy + offset]->name(replacements);
      MEs_[kShape + offset]->name(replacements);
      MEs_[kTiming + offset]->name(replacements);
      MEs_[kAOverP + offset]->name(replacements);

      for(vector<int>::iterator gainItr(MGPAGainsPN_.begin()); gainItr != MGPAGainsPN_.end(); ++gainItr){
	ss.str("");
	ss << *gainItr;
	replacements["pngain"] = ss.str();

	offset = (*wlItr - 1) * nPNGain + (*gainItr == 1 ? 0 : 1);

	MEs_[kPNAmplitude + offset]->name(replacements);
      }
    }
  }

  LedTask::~LedTask()
  {
  }

  void
  LedTask::bookMEs()
  {
    for(std::vector<int>::iterator wlItr(ledWavelengths_.begin()); wlItr != ledWavelengths_.end(); ++wlItr){
      unsigned offset(*wlItr - 1);

      MEs_[kAmplitudeSummary + offset]->book();
      MEs_[kAmplitude + offset]->book();
      MEs_[kOccupancy + offset]->book();
      MEs_[kTiming + offset]->book();
      MEs_[kShape + offset]->book();
      MEs_[kAOverP + offset]->book();

      for(std::vector<int>::iterator gainItr(MGPAGainsPN_.begin()); gainItr != MGPAGainsPN_.end(); ++gainItr){
	offset = (*wlItr - 1) * nPNGain + (*gainItr == 1 ? 0 : 1);

	MEs_[kPNAmplitude + offset]->book();
      }
    }

    MEs_[kPNOccupancy]->book();
  }

  void
  LedTask::beginRun(const edm::Run &, const edm::EventSetup &_es)
  {
    for(int iDCC(0); iDCC < BinService::nDCC; iDCC++){
      enable_[iDCC] = false;
      wavelength_[iDCC] = -1;
    }
    pnAmp_.clear();
  }

  void
  LedTask::endEvent(const edm::Event &, const edm::EventSetup &)
  {
    for(int iDCC(0); iDCC < BinService::nDCC; iDCC++){
      enable_[iDCC] = false;
      wavelength_[iDCC] = -1;
    }
    pnAmp_.clear();
  }

  bool
  LedTask::filterRunType(const std::vector<short>& _runType)
  {
    bool enable(false);

    for(int iDCC(0); iDCC < BinService::nDCC; iDCC++){
      if(_runType[iDCC] == EcalDCCHeaderBlock::LED_STD ||
	 _runType[iDCC] == EcalDCCHeaderBlock::LED_GAP){
	enable = true;
	enable_[iDCC] = true;
      }
    }

    return enable;
  }

  void
  LedTask::runOnRawData(const EcalRawDataCollection &_dcchs)
  {
    for(EcalRawDataCollection::const_iterator dcchItr(_dcchs.begin()); dcchItr != _dcchs.end(); ++dcchItr){
      int iDCC(dcchItr->id() - 1);

      if(!enable_[iDCC]) continue;

      wavelength_[iDCC] = dcchItr->getEventSettings().wavelength + 1;

      if(std::find(ledWavelengths_.begin(), ledWavelengths_.end(), wavelength_[iDCC]) == ledWavelengths_.end()) enable_[iDCC] = false;
    }
  }

  void
  LedTask::runOnDigis(const EcalDigiCollection &_digis)
  {
    for(EcalDigiCollection::const_iterator digiItr(_digis.begin()); digiItr != _digis.end(); ++digiItr){
      const DetId& id(digiItr->id());

      int iDCC(dccId(id) - 1);

      if(!enable_[iDCC]) continue;

      unsigned offset(-1);
      switch(wavelength_[iDCC]){
      case 1: offset = 0; break;
      case 3: offset = 1; break;
      default: continue;
      }

      MEs_[kOccupancy + offset]->fill(id);

      // EcalDataFrame is not a derived class of edm::DataFrame, but can take edm::DataFrame in the constructor
      EcalDataFrame dataFrame(*digiItr);

      for(int iSample(0); iSample < 10; iSample++)
	MEs_[kShape + offset]->fill(id, iSample + 0.5, float(dataFrame.sample(iSample).adc()));
    }
  }

  void
  LedTask::runOnPnDigis(const EcalPnDiodeDigiCollection &_digis)
  {
    for(EcalPnDiodeDigiCollection::const_iterator digiItr(_digis.begin()); digiItr != _digis.end(); ++digiItr){
      const EcalPnDiodeDetId& id(digiItr->id());

      int iDCC(dccId(id) - 1);

      if(!enable_[iDCC]) continue;

      MEs_[kPNOccupancy]->fill(id);

      float pedestal(0.);
      for(int iSample(0); iSample < 4; iSample++)
	pedestal += digiItr->sample(iSample).adc();
      pedestal /= 4.;

      float max(0.);
      for(int iSample(0); iSample < 50; iSample++){
	EcalFEMSample sample(digiItr->sample(iSample));

	float amp(digiItr->sample(iSample).adc() - pedestal);

	if(amp > max) max = amp;
      }

      int gain(digiItr->sample(0).gainId() == 0 ? 1 : 16);
      max *= (16. / gain);

      unsigned offset(-1);
      switch(wavelength_[iDCC]){
      case 1: offset = (gain == 1 ? 0 : 1); break;
      case 3: offset = nPNGain + (gain == 1 ? 0 : 1); break;
      default: continue;
      }

      MEs_[kPNAmplitude + offset]->fill(id, max);

      if(pnAmp_.find(iDCC) == pnAmp_.end()) pnAmp_[iDCC].resize(10);
      pnAmp_[iDCC][id.iPnId() - 1] = max;
    }
  }

  void
  LedTask::runOnUncalibRecHits(const EcalUncalibratedRecHitCollection &_uhits)
  {
    using namespace std;

    for(EcalUncalibratedRecHitCollection::const_iterator uhitItr(_uhits.begin()); uhitItr != _uhits.end(); ++uhitItr){
      EEDetId id(uhitItr->id());

      int iDCC(dccId(id) - 1);

      if(!enable_[iDCC]) continue;

      unsigned offset(-1);
      switch(wavelength_[iDCC]){
      case 1: offset = 0; break;
      case 3: offset = 1; break;
      default: continue;
      }

      float amp(max((double)uhitItr->amplitude(), 0.));
      float jitter(max((double)uhitItr->jitter() + 5.0, 0.));

      MEs_[kAmplitudeSummary + offset]->fill(id, amp);
      MEs_[kAmplitude + offset]->fill(id, amp);
      MEs_[kTiming + offset]->fill(id, jitter);

      if(pnAmp_.find(iDCC) == pnAmp_.end()) continue;

      float aop(0.);
      float pn0(0.), pn1(0.);

      EcalScDetId scid(id.sc());

      int dee(MEEEGeom::dee(scid.ix(), scid.iy(), scid.zside()));
      int lmmod(MEEEGeom::lmmod(scid.ix(), scid.iy()));
      pair<int, int> pnPair(MEEEGeom::pn(dee, lmmod));

      int pnAFED(getEEPnDCC(dee, 0)), pnBFED(getEEPnDCC(dee, 1));

      pn0 = pnAmp_[pnAFED][pnPair.first];
      pn1 = pnAmp_[pnBFED][pnPair.second];

      if(pn0 < 10 && pn1 > 10){
	aop = amp / pn1;
      }else if(pn0 > 10 && pn1 < 10){
	aop = amp / pn0;
      }else if(pn0 + pn1 > 1){
	aop = amp / (0.5 * (pn0 + pn1));
      }else{
	aop = 1000.;
      }

      MEs_[kAOverP + offset]->fill(id, aop);
    }
  }

  /*static*/
  void
  LedTask::setMEData(std::vector<MEData>& _data)
  {
    BinService::AxisSpecs axis;
    axis.nbins = 10;
    axis.low = 0.;
    axis.high = 10.;

    for(unsigned iWL(0); iWL < nWL; iWL++){
      _data[kAmplitudeSummary + iWL] = MEData("AmplitudeSummary", BinService::kEcal2P, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TPROFILE2D);
      _data[kAmplitude + iWL] = MEData("Amplitude", BinService::kSM, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TPROFILE2D);
      _data[kOccupancy + iWL] = MEData("Occupancy", BinService::kEcal2P, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TH2F);
      _data[kShape + iWL] = MEData("Shape", BinService::kSM, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TPROFILE2D, 0, &axis);
      _data[kTiming + iWL] = MEData("Timing", BinService::kSM, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TPROFILE2D);
      _data[kAOverP + iWL] = MEData("AOverP", BinService::kSM, BinService::kSuperCrystal, MonitorElement::DQM_KIND_TPROFILE2D);

      for(unsigned iPNGain(0); iPNGain < nPNGain; iPNGain++){
	unsigned offset(iWL * nPNGain + iPNGain);
	_data[kPNAmplitude + offset] = MEData("PNAmplitude", BinService::kSMMEM, BinService::kCrystal, MonitorElement::DQM_KIND_TPROFILE);
      }
    }
    _data[kPNOccupancy] = MEData("PNOccupancy", BinService::kEcalMEM2P, BinService::kCrystal, MonitorElement::DQM_KIND_TH2F);
  }

  DEFINE_ECALDQM_WORKER(LedTask);
}
