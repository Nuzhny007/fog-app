/****************************************************************************/
/// @file    application-logic.cpp
/// @author  Florent Kaisser
/// @date    29.09.2016
///
// iTETRIS Fog application related logic

// ===========================================================================
// included modules
// ===========================================================================
#include "application-logic.h"
#include "app-commands-subscriptions-constants.h"

#include <time.h>

#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <string>
#include "utils/log/log.h"

#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#include <unistd.h>
#endif

// ===========================================================================
// used namespaces
// ===========================================================================
using namespace std;

// ===========================================================================
// static member variables
// ===========================================================================

/////////////////////
//Application config : initialisation on start, then no modification
Area ApplicationLogic::m_camArea;
Area ApplicationLogic::m_carArea;
bool ApplicationLogic::m_camAreaIsSet = false;
bool ApplicationLogic::m_returnCarAreaIsSet = false;
int ApplicationLogic::m_appStartTimeStep;
Area ApplicationLogic::m_fogArea;        
int ApplicationLogic::m_fogStartTimeStep;
int ApplicationLogic::m_fogEndTimeStep;    
float ApplicationLogic::m_alertRadius;
int ApplicationLogic::m_alertTimeOut; 
float ApplicationLogic::m_alertSpeedlimit;   
int ApplicationLogic::m_alertInterval=1; 
int ApplicationLogic::m_durationOfSlowdown=5;   
bool ApplicationLogic::m_noAlertMessageIfIsSlowed=false;  
bool ApplicationLogic::m_alertFog=false;
bool ApplicationLogic::m_alertRSU=false; 
bool ApplicationLogic::m_alertStoppedVehicle=false; 
bool ApplicationLogic::m_vehicleBreakInFog=false;  
int ApplicationLogic::m_alertVehicleInFogId=-1; // All vehicle   
int ApplicationLogic::m_dangerousVehicle=-1; // No dangerous vehicle
/////////////////////

/////////////////////
// Counter
int ApplicationLogic::m_messageCounter = 0;
/////////////////////

/////////////////////
// State variables

//----------------------
//state of messages
vector<AppMessage> ApplicationLogic::m_messages;
//----------------------

//----------------------
// state of vehicles/cars
map<int,Vehicle> ApplicationLogic::m_vehiclesInFog;
// mapped on car id
map<int, bool> ApplicationLogic::m_carRxSubs;
map<int, int> ApplicationLogic::m_carLastSpeedChangeTime;   
map<int, int> ApplicationLogic::m_carLastSlowedTime;   
map<int, int> ApplicationLogic::m_vehiclesInFogOnceTime; 
map<int, int> ApplicationLogic::m_lastBroadcast; 
int ApplicationLogic::m_vehicleWithModifiedTau=-1; // No vehicle with modified Tau
//----------------------



/////////////////////


//////////////////////////////////////////////////
//iCS callback methods
bool
ApplicationLogic::DropSubscription(int subscriptionType)
{
   switch(subscriptionType) {
     case (SUB_SET_CAM_AREA):
     case (SUB_RETURNS_CARS_IN_ZONE):
     case (SUB_RECEIVED_CAM_INFO):
     case (SUB_APP_MSG_RECEIVE): {
       return false;
     }
     case (SUB_X_APPLICATION_DATA):
     case (SUB_APP_MSG_SEND):
     case (SUB_APP_CMD_TRAFF_SIM):
     case (SUB_APP_RESULT_TRAFF_SIM):
     case (SUB_FACILITIES_INFORMATION): {
       return true;
     }
     default: {
       return false;
     }
   } 
}

bool SortByPos (Vehicle i,Vehicle j) { 

  //TODO no work with not straight road
  return (i.x<j.x); 

}

bool
ApplicationLogic::ProcessSubscriptionCarsInZone(vector<Vehicle>& vehicles)
{
    //sort the vehicle by its positions
    std::sort (vehicles.begin(), vehicles.end(), SortByPos);  

    // Erase old data
    m_vehiclesInFog.clear();
    
    for (vector<Vehicle>::const_iterator it = vehicles.begin() ; it != vehicles.end() ; it++) {   
      Vehicle v =  *it;
         
      v.behindId = ApplicationLogic::FirstVehicleOnSameLane(vehicles,it);
      v.ttc = ApplicationLogic::ComputeTTC(v.id,v.behindId);
      
      m_vehiclesInFog.insert(std::pair<int,Vehicle>(v.id,v)); 
    }

    return true;
}


tcpip::Storage
ApplicationLogic::RequestReceiveSubscription(int nodeId, int timestep) {

    tcpip::Storage mySubsStorage;
    
    stringstream log;
    log << "APP --> [ApplicationLogic] RequestReceiveSubscription() Node ID " << nodeId << " subscribing to an APP_MSG_RECEIVE subscription at time "<<timestep<< "";
    Log::Write((log.str()).c_str(), kLogLevelInfo);
    
    std::map<int,bool>::iterator it = m_carRxSubs.find(nodeId);
    if ((it == m_carRxSubs.end()) || ((*it).second == false)) { // means that car with ID nodeId has not registered a RX subscription yet
      if(it == m_carRxSubs.end()) {
        m_carRxSubs.insert(std::pair<int,bool>(nodeId,true)); 
      }        
      else {
        (*it).second == true;  // will not ask twice...
      }

    if (m_alertFog || m_alertStoppedVehicle || m_dangerousVehicle > -1)
	    CreateGeobroadcastReceiveSubscription(nodeId, timestep, mySubsStorage);
	    
	  if (m_alertRSU)
	    CreateAPReceiveSubscription(nodeId, timestep, mySubsStorage);
  
  }
  return mySubsStorage;
}


bool
ApplicationLogic::ProcessReceptionOfApplicationMessage(vector<AppMessage>& messages){

    // Loop messages received by the APP_MSG_RECEIVE Subscription
    for (vector<AppMessage>::iterator it = messages.begin(); it != messages.end() ; ++it) {

        int destinationId = (*it).destinationId;

        // Match message with the registered by the App
        for (vector<AppMessage>::iterator it_ = m_messages.begin(); it_ != m_messages.end() ; ++it_) {
            int inputId = (*it).messageId; // received from the subscription
            AppMessage& registeredMsg = *it_;
            int registeredId = registeredMsg.messageId; // id registered by App
            TrafficApplicationResultMessageState status =  registeredMsg.status;
            
            if ((inputId == registeredId) && (status == kScheduled || status == kToBeApplied)) {
               
                UpdateAppMessageStatus(registeredMsg, kToBeApplied);
                AddReceiverAppMessage(registeredMsg, destinationId);
                
                stringstream log;
                log << "APP --> [ApplicationLogic] ProcessReceptionOfApplicationMessage() Message " << (*it).messageId << " received by " <<  destinationId;
                Log::Write((log.str()).c_str(), kLogLevelInfo); 
                
            } 
        }
    }
    return  true;
}

// This method is kept here to keep the global framework of iTETRIS APPs, but it is never called, as not required by this version of the DEMO APP
bool
ApplicationLogic::ProcessMessageNotifications(vector<AppMessage>& messages)
{
    // Show registered messages (logging purposes only)
    if (m_messages.size() == 0) {
        Log::Write("ProcessMessageNotifications() There are no registered messages", kLogLevelInfo);
        return true;
    } else {
        stringstream messagesOut;
        for (vector<AppMessage>::iterator it = m_messages.begin(); it != m_messages.end() ; it++) {
            AppMessage message = *it;
            if (it == m_messages.begin()) {
                messagesOut << "ProcessMessageNotifications() Registered Messages: " << message.messageId << ", ";
            } else if (it == messages.end()) {
                messagesOut << message.messageId;
            } else {
                messagesOut << message.messageId << ", ";
            }
        }
        Log::Write((messagesOut.str()).c_str(), kLogLevelInfo);
    }

    return  true;
}

                         
                           

/* We cycle here at each request for new subscription and see if we have messages that are:
 *       - in stage kToBeScheduled. If so,
 *           we change to kScheduled and request to broadcast a message
 *       - in stage kToBeApplied, If so,
 *           we change to kApplied and request to send an action
*/
tcpip::Storage 
ApplicationLogic::CheckForRequiredSubscriptions (int nodeId, int timestep){
  
    tcpip::Storage mySubsStorage;

    for (vector<AppMessage>::iterator it_ = m_messages.begin(); it_ != m_messages.end() ; ++it_) {

        if (((*it_).status == kToBeScheduled) && ((*it_).senderId == nodeId)) { // we need to broadcast a message
            UpdateAppMessageStatus((*it_), kScheduled); 
            
            if(IsAP((*it_).senderId))
              CreateAPSendSubscription(timestep, (*it_).senderId , (*it_).messageId, mySubsStorage);
            else
              CreateGeobroadcastSendSubscription(timestep, (*it_).senderId , (*it_).messageId, mySubsStorage);
              
            return  mySubsStorage;  // we break on each successful occurence, as the iCS can only read ONE subscription at a time..we will be called again by the iCS as long as we have something to request
        }
        else if (((*it_).status == kToBeApplied) && ((*it_).senderId == nodeId)) { // we need to process message

            if ((*it_).receivedIds.size() == 1)           
                UpdateAppMessageStatus((*it_), kApplied); 
        
            //for each destination of this message            
            while( ! (*it_).receivedIds.empty()){
              int destinationId = (*it_).receivedIds.back();
              (*it_).receivedIds.pop_back();
              if(LauchAction(timestep,(*it_).action,nodeId,destinationId,mySubsStorage))
                  return mySubsStorage; 
            }
        }
    }

    //check if alert is expired
    if (AlertIsExpired(nodeId,timestep)){
        //Restore initial speed limit
        ResetSpeed(timestep,nodeId,mySubsStorage);
        return  mySubsStorage;   
    }
    
    //check if alert slow down is expired
    if (NodeEndingSlowed(nodeId,timestep)){
        //End of slow down -> set reduce speed
        EndSlowDown(timestep,nodeId, mySubsStorage);
        return  mySubsStorage; 
    } 

    //Notify no subscription
    return  mySubsStorage; 
}


// even though we do not need to return a value to the iCS result container (we use the VOID_RESULT_CONTAINER),  
// we still need to initialize the procedure...
vector<AppMessage>
ApplicationLogic::SendBackExecutionResults(int senderId, int timestep)
{
    bool isSlowed = NodeIsSlowed(senderId,timestep);
    vector<AppMessage> results;
    if (     timestep >= m_appStartTimeStep  ) { 
        /*stringstream log;
         log<< "test nedd braodcast " <<  m_alertFog
          <<  !IsAP(senderId) 
          <<  FogIsActive(timestep) 
          <<  IsInFog(senderId) 
          <<  !(m_noAlertMessageIfIsSlowed && isSlowed) 
          <<  IsTimeToSendAlert(senderId,timestep)
          <<  IsBroadcaster(senderId);
          Log::Write((log.str()).c_str(), kLogLevelInfo);*/
         
	    //test if the vehicle need to broacast a new alert
        if(  m_alertFog
          && !IsAP(senderId) 
          && FogIsActive(timestep) 
          && IsInFog(senderId) 
          && !(m_noAlertMessageIfIsSlowed && isSlowed) 
          && IsTimeToSendAlert(senderId,timestep)
          && IsBroadcaster(senderId)
        ){
          //Broadcast the alert
          stringstream log;
          log<< "Vehicle " << senderId << " broadcast alert at " << timestep;
          Log::Write((log.str()).c_str(), kLogLevelInfo);
          
          NewAppMessage(timestep,senderId,VALUE_SLOW_DOWN,kToBeScheduled);
          
          //keep time when last broadcast
          m_lastBroadcast.erase(senderId);
          m_lastBroadcast.insert(std::pair<int,int>(senderId,timestep));
        }		
        
        if(  m_alertRSU 
          && IsAP(senderId) 
          && FogIsActive(timestep) 
        ){
        
          //Broadcast the alert
          stringstream log;
          log<< "RSU " << senderId << " broadcast alert at " << timestep;
          Log::Write((log.str()).c_str(), kLogLevelInfo);
          
          NewAppMessage(timestep,senderId,VALUE_SLOW_DOWN,kToBeScheduled);    
        }
        
        if( m_alertStoppedVehicle
          && !IsAP(senderId)
          && IsStoppedInFog(senderId)
        ){
        
           //Broadcast the alert
          stringstream log;
          log<< "Stopped Vehicle " << senderId << " broadcast alert at " << timestep;
          Log::Write((log.str()).c_str(), kLogLevelInfo);
          
          NewAppMessage(timestep,senderId,VALUE_CHANGE_LANE,kToBeScheduled);           
        
        }
                   
        //Test if is the first time which vehicle enter in alert zone
        if( m_vehicleBreakInFog 
        	&& IsInFog(senderId) 
        	&& (m_vehiclesInFogOnceTime.find(senderId) == m_vehiclesInFogOnceTime.end())){
          //For statistics
          ProcessStatistic(timestep,senderId,isSlowed);
          //I must reduce my speed ! 		      
	    		NewAppMessage(timestep,senderId,VALUE_SET_SPEED,kToBeApplied);
	      }
	      
	     if( IsDangerousVehicle(senderId) )
         {
        
          //Broadcast the alert
          stringstream log;
          log<< "Dangerouse vehicle " << senderId << " broadcast alert at " << timestep;
          Log::Write((log.str()).c_str(), kLogLevelInfo);
          
          NewAppMessage(timestep,senderId,DANGEROUS_VEHICLE,kToBeScheduled);    
        }
	    
      // Keep in safe place all the results to send back to the iCS
      results = m_messages; 	

    }
    
    // Loop current messages to find those to be applied and erase from the result record
    for (vector<AppMessage>::iterator it = m_messages.begin() ; it != m_messages.end() ; it++) {
        //if ((*it).status == kApplied) {  
        if ((*it).status == kToBeDiscarded) {
            m_messages.erase(it);
            stringstream log;
           	log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Message " << (*it).messageId << " was kApplied...has been removed, as the cycle is now complete.";
          	Log::Write((log.str()).c_str(), kLogLevelInfo); 
        }
    }

    return results;
}
//END iCS callback methods
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Support methods

void ApplicationLogic::InitSubscription(unsigned char subsType, unsigned char cmdLength, tcpip::Storage& mySubsStorage) {
    // command length (size of next data (4 bytes) in this method and the size of other data)
    mySubsStorage.writeUnsignedByte(cmdLength+4);
    // command type
    mySubsStorage.writeUnsignedByte(CMD_ASK_FOR_SUBSCRIPTION);
    // subscription type
    mySubsStorage.writeUnsignedByte(subsType);
	//sends one type of message : the code is 0x01 ; if we need to send another type, please change to a different number     
	mySubsStorage.writeUnsignedByte(0X01); //HEADER_APP_MSG_TYPE 
   
}

void
ApplicationLogic::CreateGeobroadcastReceiveSubscription(int nodeId, int timestep, tcpip::Storage& mySubsStorage) {
    
    InitSubscription(SUB_APP_MSG_RECEIVE,1 + 4 + 2 + 1 + 1 + 2 + 4, mySubsStorage);
    
    // Only destID
    mySubsStorage.writeUnsignedByte(0x04); 
    //Target Node id , its for me
    mySubsStorage.writeInt(nodeId);	
    //Length of rest of the command (payload length). 
    mySubsStorage.writeShort(500);
    //Unicast transmissions
    mySubsStorage.writeUnsignedByte(EXT_HEADER_TYPE_GEOBROADCAST);
    //Additional header specifying the sources TODO : set to -1
    mySubsStorage.writeUnsignedByte(EXT_HEADER__VALUE_BLOCK_IDs);
    
    //TODO remove
    //Number of Sources (1 for broadcast)
    mySubsStorage.writeShort(1);
    //-1 for broadcast
    mySubsStorage.writeInt(-1);

}

void
ApplicationLogic::CreateAPReceiveSubscription(int nodeId, int timestep, tcpip::Storage& mySubsStorage) {

    InitSubscription(SUB_APP_MSG_RECEIVE,1 + 4 + 2 + 1 + 1 + 2, mySubsStorage);
    
    // Only destID
    mySubsStorage.writeUnsignedByte(0x04); 
    //Target Node id , its for me
    mySubsStorage.writeInt(nodeId);	
    //Length of rest of the command (payload length). 
    mySubsStorage.writeShort(500);
    //Unicast transmissions
    mySubsStorage.writeUnsignedByte(EXT_HEADER_TYPE_TOPOBROADCAST);
    //Additional header specifying the sources
    mySubsStorage.writeUnsignedByte(EXT_HEADER__VALUE_BLOCK_HOPS_No);
    //Number of hops (1 for AP)
    mySubsStorage.writeShort(1);

}

void ApplicationLogic::UpdateAppMessageStatus(AppMessage& msg, TrafficApplicationResultMessageState newStatus) {
	msg.status = newStatus;
}

void ApplicationLogic::AddReceiverAppMessage(AppMessage& msg, int receiverNodeId) {
	msg.receivedIds.push_back(receiverNodeId);
}


void ApplicationLogic::CreateGeobroadcastSendSubscription(int timestep, int senderId, int messageId, tcpip::Storage& mySubsStorage){
	
	InitSubscription(SUB_APP_MSG_SEND,1 + 1 + 1 + 4 + 1 + 2 + 4 + 1 + 1 + 1 + 1 + 4+ 4 + 4, mySubsStorage);	
	mySubsStorage.writeUnsignedByte(0x0F);  // in bits, it is: 1111 : comm profile, the prefered techno and a senderID and the message lifetime
	mySubsStorage.writeUnsignedByte(0xFF);  // unsigned char preferredRATs = 0xFF;
	mySubsStorage.writeUnsignedByte(0xFF);  // unsigned char commProfile = 0xFF;
	mySubsStorage.writeInt(senderId);
	mySubsStorage.writeUnsignedByte(2);     // unsigned int msgLifetime = 2;
	   mySubsStorage.writeShort(1024); // m_app_Msg_length
	mySubsStorage.writeInt(messageId); //sequence number
	mySubsStorage.writeUnsignedByte(EXT_HEADER_TYPE_GEOBROADCAST); // CommMode
	mySubsStorage.writeUnsignedByte(EXT_HEADER__VALUE_BLOCK_AREAs);
	mySubsStorage.writeUnsignedByte(1);
	/*mySubsStorage.writeInt((*it_).destinationId);*/

	mySubsStorage.writeUnsignedByte(EXT_HEADER__CIRCLE); //Area type 
	mySubsStorage.writeFloat(m_fogArea.x); //x
	mySubsStorage.writeFloat(m_fogArea.y); //y
	mySubsStorage.writeFloat(m_fogArea.radius + m_alertRadius);	//radius   


}

void ApplicationLogic::CreateAPSendSubscription(int timestep, int senderId, int messageId, tcpip::Storage& mySubsStorage){
	
	InitSubscription(SUB_APP_MSG_SEND,1 + 1 + 1 + 4 + 1 + 2 + 4 + 1 + 1 + 2, mySubsStorage);	
	mySubsStorage.writeUnsignedByte(0x0F);  // in bits, it is: 1111 : comm profile, the prefered techno and a senderID and the message lifetime
	mySubsStorage.writeUnsignedByte(0xFF);  // unsigned char preferredRATs = 0xFF;
	mySubsStorage.writeUnsignedByte(0xFF);  // unsigned char commProfile = 0xFF;
	mySubsStorage.writeInt(senderId);
	mySubsStorage.writeUnsignedByte(2);     // unsigned int msgLifetime = 2;
	   mySubsStorage.writeShort(1024); // m_app_Msg_length
	mySubsStorage.writeInt(messageId); //sequence number
	mySubsStorage.writeUnsignedByte(EXT_HEADER_TYPE_TOPOBROADCAST); // CommMode
	mySubsStorage.writeUnsignedByte(EXT_HEADER__VALUE_BLOCK_HOPS_No); 
  //Number of hops (1 for AP)
  mySubsStorage.writeShort(1);
}


void ApplicationLogic::CreateSetSpeedTrafficSimSubscription(int timestep, int destinationId, float speedLimit, tcpip::Storage& mySubsStorage){

	InitSubscription(SUB_APP_CMD_TRAFF_SIM,1 + 4 + 4 , mySubsStorage);	
	mySubsStorage.writeUnsignedByte(VALUE_SET_SPEED); //to change the speed
	mySubsStorage.writeInt(destinationId); //Destination node to set its maximum speed.
	mySubsStorage.writeFloat(speedLimit); //Set Maximum speed

}

void ApplicationLogic::CreateSlowDownTrafficSimSubscription(int timestep, int destinationId, float speedLimit, int durationOfSlowdown, tcpip::Storage& mySubsStorage){
    InitSubscription(SUB_APP_CMD_TRAFF_SIM, 1 + 4 + 4 + 4, mySubsStorage);	
	mySubsStorage.writeUnsignedByte(VALUE_SLOW_DOWN); //to change the speed
	mySubsStorage.writeInt(destinationId); //Destination node to set its maximum speed.
	mySubsStorage.writeFloat(speedLimit); //Set Maximum speed
	mySubsStorage.writeInt(durationOfSlowdown); //duration of slow down                      
                           
}  


void ApplicationLogic::CreateChangeLaneSimSubscription(int timestep, int destinationId, unsigned char idLane, int durationOfChangedLane, tcpip::Storage& mySubsStorage){
    InitSubscription(SUB_APP_CMD_TRAFF_SIM, 1 + 4 + 1 + 4, mySubsStorage);	
	mySubsStorage.writeUnsignedByte(VALUE_CHANGE_LANE); //to change the speed
	mySubsStorage.writeInt(destinationId); //Destination node to set its maximum speed.
	mySubsStorage.writeChar(idLane); //id of lane
	mySubsStorage.writeInt(durationOfChangedLane); //duration of lane chage                      
                           
}  

void ApplicationLogic::CreateChangeTauSimSubscription(int timestep, int destinationId, float tau, tcpip::Storage& mySubsStorage){
    InitSubscription(SUB_APP_CMD_TRAFF_SIM, 1 + 4 + 4, mySubsStorage);	
	mySubsStorage.writeUnsignedByte(VALUE_TAU); //to change the speed
	mySubsStorage.writeInt(destinationId); //Destination node to set its maximum speed.
	mySubsStorage.writeFloat(tau); //id of lane   
}

void ApplicationLogic::Break(int timestep, int destinationId,  tcpip::Storage& mySubsStorage) {
    stringstream log;
    
    if(IsStoppedInFog(destinationId))
      return;
    
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " break !";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    CreateSetSpeedTrafficSimSubscription(timestep,destinationId,m_alertSpeedlimit,mySubsStorage);

    //if in slowdown, stop slowdown
    m_carLastSlowedTime.erase(destinationId);
    
    //keep the time when vehicle change her speed
    m_carLastSpeedChangeTime.erase(destinationId);
    m_carLastSpeedChangeTime.insert(std::pair<int,int>(destinationId,timestep)); 
}
 

void ApplicationLogic::StartSlowDown(int timestep, int destinationId, tcpip::Storage& mySubsStorage) {

    stringstream log;
    
    if(IsStoppedInFog(destinationId))
      return;    
    
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " slowed !";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    CreateSlowDownTrafficSimSubscription(timestep,destinationId, m_alertSpeedlimit, m_durationOfSlowdown * 1000, mySubsStorage);
    
    //keep the time when vehicle slowdown
    m_carLastSlowedTime.erase(destinationId);
    m_carLastSlowedTime.insert(std::pair<int,int>(destinationId,timestep)); 
    
    //keep the time when vehicle change her speed
    m_carLastSpeedChangeTime.erase(destinationId);
    m_carLastSpeedChangeTime.insert(std::pair<int,int>(destinationId,timestep));   
}

void ApplicationLogic::EndSlowDown(int timestep, int destinationId,  tcpip::Storage& mySubsStorage) {
    stringstream log;
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " end of slowing   !";
    Log::Write((log.str()).c_str(), kLogLevelInfo);
      
    CreateSetSpeedTrafficSimSubscription(timestep,destinationId,m_alertSpeedlimit,mySubsStorage);
    m_carLastSlowedTime.erase(destinationId);

}

void ApplicationLogic::ResetSpeed(int timestep, int destinationId, tcpip::Storage& mySubsStorage) {
    stringstream log;
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " speed restored !";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    CreateSetSpeedTrafficSimSubscription(timestep,destinationId,-1,mySubsStorage);
    m_carLastSpeedChangeTime.erase(destinationId);
}

void ApplicationLogic::ChangeLane(int timestep, int destinationId, tcpip::Storage& mySubsStorage) {

    stringstream log;
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " change lane !";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    CreateChangeLaneSimSubscription(timestep, destinationId, 1, 90, mySubsStorage);
}

void ApplicationLogic::ChangeTau(int timestep, int nodeId, float tau, tcpip::Storage& mySubsStorage) {
    stringstream log;
    log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << nodeId << " tau changed at " << tau << " !";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 
    
    if(m_vehicleWithModifiedTau != nodeId){
      m_vehicleWithModifiedTau = nodeId;
      CreateChangeTauSimSubscription(timestep, nodeId, tau, mySubsStorage);
    }
}

void ApplicationLogic::DangerousVehicleAction(int timestep, int dangerousVehicleId , int nodeId, tcpip::Storage& mySubsStorage) {
    stringstream log;
    log << "APP --> [ApplicationLogic] LauchAction DANGEROUS_VEHICLE " << dangerousVehicleId << " " << nodeId <<  " ";
    Vehicle v = GetVehicle(nodeId);
    
    //get the behind vehicle
    if (v.id < 0 || v.behindId < 0){
      log << -1;
    } else {
      log << v.behindId;
      
      Log::Write((log.str()).c_str(), kLogLevelError);
      
      if(v.behindId == dangerousVehicleId){
        if (m_vehicleWithModifiedTau > -1)
          ChangeTau(timestep, m_vehicleWithModifiedTau, 1.0, mySubsStorage);
        ChangeTau(timestep, nodeId, 5.0, mySubsStorage);  
      }
    }             
}

bool ApplicationLogic::LauchAction(int timestep, int action, int senderId , int destinationId, tcpip::Storage& mySubsStorage) {
                           
      stringstream log;
      log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() action " << " action";
      Log::Write((log.str()).c_str(), kLogLevelError);   

    if(IsAP(senderId) || IsBehind(senderId,destinationId)){    
    
      stringstream log;
      log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() ids" << senderId << " " << destinationId;
      Log::Write((log.str()).c_str(), kLogLevelError);     
                  
      switch (action){
          case VALUE_SLOW_DOWN:
              StartSlowDown(timestep, destinationId, mySubsStorage);
              return true;
              
          case VALUE_SET_SPEED:
              Break(timestep, destinationId, mySubsStorage);
              return true;   

          case VALUE_CHANGE_LANE:
              ChangeLane(timestep, destinationId, mySubsStorage);
              return true;   
              
          case DANGEROUS_VEHICLE:
              DangerousVehicleAction(timestep, senderId, destinationId, mySubsStorage);
              return true;
                      
          default :
          {
              stringstream log;
              log << "APP --> [ApplicationLogic] CheckForRequiredSubscriptions() Node Vehicle " << destinationId << " bad action";
              Log::Write((log.str()).c_str(), kLogLevelError); 
          }                       
      }
    }
    return false;
}                           
                           

void ApplicationLogic::NewAppMessage(int timestep, int senderId, int action, TrafficApplicationResultMessageState initStatus){

	//Init message
	AppMessage message;
	message.senderId = senderId;
	message.destinationId = 0; //broadcast !
	message.createdTimeStep = timestep;
	message.messageId = ++m_messageCounter;
	message.status = initStatus;        
	message.action = action; 
	
	if(initStatus == kToBeApplied)
		message.receivedIds.push_back(senderId);
	
	m_messages.push_back(message);

}

void ApplicationLogic::ProcessStatistic(int timestep, int senderId, bool isSlowed){

    //The vehicle is enter in fog now
    stringstream log; 
    log << "Vehicle " << senderId << "enter in hazard zone at ";

    if(isSlowed){
        //Vehicle already slowed (message recevied)
        log << timestep;
        Log::Write((log.str()).c_str(), kLogLevelInfo);
        m_vehiclesInFogOnceTime.insert(std::pair<int,int>(senderId,timestep));
    }else{ 
        //Vehicle no slowed
        log << -1;
        Log::Write((log.str()).c_str(), kLogLevelInfo);
        m_vehiclesInFogOnceTime.insert(std::pair<int,int>(senderId,-1)); 
    }

} 

int ApplicationLogic::FirstVehicleOnSameLane(vector<Vehicle>& vehicles,vector<Vehicle>::const_iterator v) {

	if(v->direction == 90){
		
		for (vector<Vehicle>::const_iterator it = v+1; it != vehicles.end() ; it++) {  
			if(it->y == v->y)
				return it->id;
		}
	
	}
	
	if(v->direction == 270 && v != vehicles.begin()){
		
		for (vector<Vehicle>::const_iterator it = v-1; it != vehicles.begin() ; it--) {  
			if(it->y == v->y)
				return it->id;
		}
	
	}	
	
	return -1;
}

bool 
ApplicationLogic::IsTimeToSendAlert(int nodeId, int timestep){
    std::map<int,int>::iterator it = m_lastBroadcast.find(nodeId);


    if (it == m_lastBroadcast.end()) 
        //This vehicle has never send alert
        return true;
    else if (timestep >= it->second+m_alertInterval)
        //This vehicle has send an alert longer than the interval delay
        return true;
    else {
        //This vehicle has send an alert less than the interval delay
        return false;
    }

    
    //return msg.status == kApplied && (timestep > msg.createdTimeStep+m_alertTimeOut);
}


bool 
ApplicationLogic::AlertIsExpired(int nodeId, int timestep){
    std::map<int,int>::iterator it = m_carLastSpeedChangeTime.find(nodeId);


    if (it == m_carLastSpeedChangeTime.end()) 
        //This vehicle has not changed her speed
        return false;
    else if (timestep <= it->second+m_alertTimeOut)
        //This vehicle has changed her speed, but the alert has not expired
        return false;
    else {
        //This vehicle has changed her speed, but the alert has expired
        m_carLastSpeedChangeTime.erase (it);
        return true;
    }

    
    //return msg.status == kApplied && (timestep > msg.createdTimeStep+m_alertTimeOut);
}

bool 
ApplicationLogic::NodeEndingSlowed(int nodeId, int timestep){
    std::map<int,int>::iterator it = m_carLastSlowedTime.find(nodeId);


    if (it == m_carLastSlowedTime.end()) 
        //This vehicle has not changed her speed
        return false;
    else if (timestep >= it->second+m_durationOfSlowdown)
        return true;
    else {
        return false;
    }
}


bool 
ApplicationLogic::NodeIsSlowed(int nodeId, int timestep){
    std::map<int,int>::iterator it = m_carLastSpeedChangeTime.find(nodeId);


    if (it == m_carLastSpeedChangeTime.end()) 
        //This vehicle has not changed her speed
        return false;
    else if (timestep <= it->second+m_alertTimeOut)
        //This vehicle has changed her speed, but the alert has not expired
        return true;
    else {
        //This vehicle has changed her speed, but the alert has expired
        return false;
    }

    
    //return msg.status == kApplied && (timestep > msg.createdTimeStep+m_alertTimeOut);
}


bool 
ApplicationLogic::FogIsActive(int timestep){
    return (m_fogStartTimeStep <= timestep) && (timestep < m_fogEndTimeStep);
}



Vehicle
ApplicationLogic::GetVehicle(int idNode){
    Vehicle v;
    
    v.id=-1;
    std::map<int,Vehicle>::iterator it = m_vehiclesInFog.find(idNode);

    if(it != m_vehiclesInFog.end())
      v = it->second;

    return v;
}

bool
ApplicationLogic::IsInFog(int idNode){
    return GetVehicle(idNode).id!=-1;
}

bool ApplicationLogic::IsBroadcaster(int idNode){
    return  IsAP(idNode) 
            || (m_alertVehicleInFogId ==-1)
            || (idNode == m_alertVehicleInFogId);
}


float 
ApplicationLogic::ComputeDistance(int idNode1,int idNode2){
    Vehicle v1 = GetVehicle(idNode1);
    Vehicle v2 = GetVehicle(idNode2); 
    	
	//TODO no work with not straight road
	return v2.x - v1.x;
}

float 
ApplicationLogic::ComputeRelSpeed(int idNode1,int idNode2){
    Vehicle v1 = GetVehicle(idNode1);
    Vehicle v2 = GetVehicle(idNode2); 
    
    if(v1.id * v2.id < 0)
    	return -1.0;
    	
	return v2.speed - v1.speed;
}

float 
ApplicationLogic::ComputeTTC(int idNode1,int idNode2){
	return (ComputeDistance(idNode1,idNode2) - VEHICLE_LENGTH) 
			/ ComputeRelSpeed(idNode1, idNode2) ;
} 

bool
ApplicationLogic::IsSameDirection(int idNode1,int idNode2){
    Vehicle v1 = GetVehicle(idNode1);
    Vehicle v2 = GetVehicle(idNode2);    

    if(v1.id==-1 || v2.id==-1)
      return false;

    float diff = (v1.direction - v2.direction);
    return diff < 10 && diff > -10;
}

bool
ApplicationLogic::IsBehind(int idNode1,int idNode2){
    Vehicle v1 = GetVehicle(idNode1);
    Vehicle v2 = GetVehicle(idNode2);    

    if(v1.id==-1 || v2.id==-1)
      return false;

    float diff = (v1.direction - v2.direction);
    
    if ( diff > 10 || diff < -10)
      return false;
      
    //TODO no work with not straight road
    
    if(v1.direction == 90 && v1.x < v2.x)
      return false;
      
    if(v1.direction == 270 && v1.x > v2.x)
      return false;      
    
    return true;
}

bool
ApplicationLogic::IsStoppedInFog(int idNode){
    Vehicle v = GetVehicle(idNode);
    return (v.id!=-1 && v.speed < 0.1);
}

bool 
ApplicationLogic::IsDangerousVehicle(int nodeId){
  return nodeId == m_dangerousVehicle;
}

float 
ApplicationLogic::PartOfVehicleInFogSlowed(int idNode){
    int slowed=0;

    if (m_vehiclesInFogOnceTime.size()==0)
        return 1.0;

    for (std::map<int,int>::iterator it = m_vehiclesInFogOnceTime.begin() ; it != m_vehiclesInFogOnceTime.end() ; it++) {
        if(it->second >= 0)
            slowed++;
    }
    
    return (float)slowed / (float)m_vehiclesInFogOnceTime.size();
}



bool
ApplicationLogic::msgIsReceivedByNode(AppMessage& msg, int idNode){

    for (vector<int>::const_iterator itDestIds = msg.receivedIds.begin() ; itDestIds != msg.receivedIds.end() ; itDestIds++) {
        if ((*itDestIds) == idNode)
            return true;
    }
    
    return false;
}
// END Support methods
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Setters

int
ApplicationLogic::SetCamArea(float x, float y, float radius)
{

stringstream log;
    log << "CAM Area : " << "(" << x << "," << y << "," << radius << ")";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (radius <= 0) {
        Log::Write("Radius of the CAM area is 0 or negative.", kLogLevelError);
        return EXIT_FAILURE;
    }

    m_camArea.x = x;
    m_camArea.y = y;
    m_camArea.radius = radius;

    return EXIT_SUCCESS;
}

int
ApplicationLogic::SetCarArea(float x, float y, float radius)
{
    stringstream log;
    log << "Car Area : " << "(" << x << "," << y << "," << radius << ")";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (radius <= 0) {
        Log::Write("Radius of the returning car area is 0 or negative.", kLogLevelError);
        return EXIT_FAILURE;
    }

    m_carArea.x = x;
    m_carArea.y = y;
    m_carArea.radius = radius;

    return EXIT_SUCCESS;
}

int
ApplicationLogic::SetApplicationStartTimeStep(int timestep)
{
    stringstream log;
    log << "Start time : " << timestep;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (timestep < 0) {
        Log::Write("Application start time step is negative", kLogLevelError);
        return EXIT_FAILURE;
    }

    m_appStartTimeStep = timestep;

    return EXIT_SUCCESS;
}


int ApplicationLogic::SetFogArea(float x, float y, float radius)
{
    stringstream log;
    log << "Fog Area : " << "(" << x << "," << y << "," << radius << ")";
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (radius <= 0) {
        Log::Write("Radius of the returning fog area is 0 or negative.", kLogLevelError);
        return EXIT_FAILURE;
    }

    m_fogArea.x = x;
    m_fogArea.y = y;
    m_fogArea.radius = radius;
    return EXIT_SUCCESS;     
}

int ApplicationLogic::SetFogStartTimeStep(int timestep)
{
    stringstream log;
    log << "Start fog time : " << timestep;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (timestep < 0) {
        Log::Write("Fog start time step is negative", kLogLevelError);
        return EXIT_FAILURE;
    }

    m_fogStartTimeStep = timestep;
    return EXIT_SUCCESS;     
}    

int ApplicationLogic::SetFogEndTimeStep(int timestep)
{
    stringstream log;
    log << "End fog time : " << timestep;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (timestep < 0) {
        Log::Write("Fog over time step is negative", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_fogEndTimeStep = timestep;
    return EXIT_SUCCESS;     
}        

int ApplicationLogic::SetAlertRadius(float radius)
{
    stringstream log;
    log << "Alert radius : " << radius;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (radius <= 0) {
        Log::Write("Radius of alert is 0 or negative.", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_alertRadius = radius;

    return EXIT_SUCCESS;     
}    

int ApplicationLogic::SetAlertTimeOut(int alerttimeout)
{
    stringstream log;
    log << "Alert timeout : " << alerttimeout;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (alerttimeout < 0) {
        Log::Write("Alert time out is negative", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_alertTimeOut = alerttimeout;

    return EXIT_SUCCESS;     
}     

int ApplicationLogic::SetAlertSpeedlimit(float alertspeedlimit)
{
    stringstream log;
    log << "Alert speed limit : " << alertspeedlimit;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (alertspeedlimit < 0) {
        Log::Write("Alert speed limit is negative", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_alertSpeedlimit = alertspeedlimit;

    return EXIT_SUCCESS;     
}       

int ApplicationLogic::SetVehicleAlert(bool actif)
{
    stringstream log;
    if(actif)
      log << "Vehicle Fog Alert is activ";
    else
      log << "Vehicle Fog Alert is inactiv";    
      
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_alertFog = actif;
   
    return EXIT_SUCCESS;      
}

int ApplicationLogic::SetRSUAlert(bool actif)
{
    stringstream log;
    if(actif)
      log << "RSU Alert is activ";
    else
      log << "RSU Alert is inactiv";    
      
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_alertRSU = actif;
   
    return EXIT_SUCCESS;      
}

int ApplicationLogic::SetStoppedVehicleAlert(bool actif)
{
    stringstream log;
    if(actif)
      log << "Stopped Vehicle Alert is activ";
    else
      log << "Stopped Vehicle Alert is inactiv";    
      
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_alertStoppedVehicle = actif;
   
    return EXIT_SUCCESS;      
}

int ApplicationLogic::SetAlertInterval(int interval){
    stringstream log;
    log << "Alert interval minimum : " << interval;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (interval < 0) {
        Log::Write("Interval is negative", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_alertInterval = interval;

    return EXIT_SUCCESS;  
}

int ApplicationLogic::SetDurationOfSlowdown(int duration){
    stringstream log;
    log << "Duration of slowdown : " << duration;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    if (duration < 0) {
        Log::Write("Duration is negative", kLogLevelError);
        return EXIT_FAILURE;
    }
    
    m_durationOfSlowdown = duration;

    return EXIT_SUCCESS;  
}

int ApplicationLogic::SetNoAlertMessageIfIsSlowed(bool enable){
    stringstream log;
    if(enable)
      log << "NoAlertMessageIfIsSlowed is activ";
    else
      log << "NoAlertMessageIfIsSlowed is inactiv";    
      
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_noAlertMessageIfIsSlowed = enable;
   
    return EXIT_SUCCESS;   
}  

int ApplicationLogic::SetBreakInFog(bool enable){
    stringstream log;
    if(enable)
      log << "Break inf fog is activ";
    else
      log << "Break inf fog is inactiv";    
      
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_vehicleBreakInFog = enable;
   
    return EXIT_SUCCESS;   
}  

int ApplicationLogic::SetAlertVehicleInFogId(int id){

    stringstream log;
    log << "Id of vehicle which send alert : " << id;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_alertVehicleInFogId = id;

    return EXIT_SUCCESS;   
}  

int ApplicationLogic::SetDangerousVehicle(int id){

    stringstream log;
    log << "Id of dangerous : " << id;
    Log::Write((log.str()).c_str(), kLogLevelInfo); 

    m_dangerousVehicle = id;

    return EXIT_SUCCESS;   
} 
    

     

//END Setters
//////////////////////////////////////////////////

//////////////////////////////////////////////////
// Getters

Area ApplicationLogic::GetCamArea()
{
    m_camAreaIsSet = true;
    return m_camArea;
}

Area ApplicationLogic::GetReturningCarArea()
{
    m_returnCarAreaIsSet = true;
    return m_carArea;
}

//////////////////////////////////////////////////
