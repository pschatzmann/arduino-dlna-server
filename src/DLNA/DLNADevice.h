#pragma once

#include "DLNADevice.h"
#include "DLNADeviceInfo.h"
#include "DLNARequestParser.h"
#include "Schedule.h"
#include "TinyHttp/HttpServer.h"
#include "TinyHttp/Server/Url.h"

namespace tiny_dlna {

/**
 * @brief Abstract DLNA Device service setup. The device registers itself to the
 * network and answers to the DLNA queries and requests.
 * A DLNA device uses UDP, Http, XML and Soap to discover and manage a service,
 * so there is quite some complexity involved:
 * - We handle the UDP communication via a Scheduler and a Request Parser
 * - We handle the Http request with the help of the TinyHttp Server
 * - The XML service descriptions can be stored as char arrays in progmem or
 * generated dynamically with the help of the XMLPrinter class.
 * @author Phil Schatzmann
 */
class DLNADevice {
 public:
  bool begin(DLNADeviceInfo& device, UDPService& udp, HttpServer& server) {
    DlnaLogger.log(DlnaInfo, "DLNADevice::begin");

    p_server = &server;
    p_udp = &udp;
    addDevice(device);

    // check base url
    Url baseUrl = device.getBaseURL();
    DlnaLogger.log(DlnaInfo, "base URL: %s", baseUrl.url());

    if (StrView(device.getBaseURL().host()).equals("localhost")) {
      DlnaLogger.log(DlnaError, "invalid base address: %s", baseUrl.url());
      return false;
    }

    for (auto& p_device : devices) setupServices(*p_device);

    setupDLNAServer(server);

    setupScheduler();

    return true;
  }

  /// We potentially support some additional devices on top of the one defined
  /// via begin
  void addDevice(DLNADeviceInfo& device) { devices.push_back(&device); }

  void end() {
    // send 3 bye messages
    PostByeSchedule bye;
    bye.end_time = millis() + 60000;
    bye.repeat_ms = 20000;
    scheduler.add(bye);

    for (auto& p_device : devices) p_device->clear();

    p_server->end();
  }

  bool loop() {
    if (!is_active) return false;

    // handle server requests
    if (p_server) p_server->doLoop();

    // process UDP requests
    RequestData req = p_udp->receive();
    Schedule schedule = parser.parse(req);
    if (schedule) {
      scheduler.add(schedule);
    }

    // execute scheduled udp replys
    scheduler.execute(*p_udp, getDevice());

    // be nice, if we have other tasks
    delay(10);

    return true;
  }

  DLNAServiceInfo getService(const char* id, int deviceIdx = 0) {
    return devices[deviceIdx]->getService(id);
  }

  DLNADeviceInfo& getDevice(int deviceIdx = 0) { return *devices[deviceIdx]; }

 protected:
  Scheduler scheduler;
  DLNARequestParser parser;
  UDPService* p_udp = nullptr;
  Vector<DLNADeviceInfo*> devices;
  HttpServer* p_server = nullptr;
  bool is_active = false;

  void setupScheduler() {
    // schedule post alive messages: Usually repeated 3 times (because UDP
    // messages might be lost)
    DlnaLogger.log(DlnaInfo, "schedule PostAliveSchedule");
    PostAliveSchedule postAlive[3];
    postAlive[1].time = millis() + 100;
    postAlive[2].time = millis() + 300;
    for (auto& schedule : postAlive) {
      scheduler.add(schedule);
    }
  }

  virtual void setupDLNAServer(HttpServer& srv) {
    auto xmlDevice = [](HttpServer* server, const char* requestPath,
                        HttpRequestHandlerLine* hl) {
      // auto cb = [&](Stream& out) { p_device->write(out); };
      server->reply("text/xml", "");
    };

    // Setup services for all devices
    for (auto& p_device : devices) {
      // add device url to server
      const char* device_path = p_device->getDeviceURL().path();
      DlnaLogger.log(DlnaInfo, "Setting up device path: %s", device_path);

      if (StrView(device_path).isEmpty()) {
        p_server->rewrite("/", device_path);
        p_server->rewrite("/index.html", device_path);
        p_server->on(device_path, T_GET, xmlDevice);
      }

      // Register Service URLs
      for (DLNAServiceInfo& service : p_device->getServices()) {
        p_server->on(service.scp_url, T_GET, service.scp_cb);
        p_server->on(service.control_url, T_POST, service.control_cb);
        p_server->on(service.event_sub_url, T_GET, service.event_sub_cb);
      }
    }
  }

  // const char* soapReplySuccess(const char* service, const char* name) {
  //   const char* tmp =
  //       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  //       "<s:Envelope "
  //       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" "
  //       "xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
  //       "<s:Body><u:%sResponse "
  //       "xmlns:u=\"urn:schemas-upnp-org:service:%s:1\">%s</u:%sResponse></"
  //       "s:Body>"
  //       "</s:Envelope>";
  //   sprintf(upnp.getTempBuffer(), tmp, service, name);
  //   return upnp.getTempBuffer();
  // }

  // const char* soapReplyDlnaError(const char* error, int errorCode) {
  //   const char* tmp =
  //       "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
  //       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
  //       "<s:Body>"
  //       "<s:Fault>"
  //       "<faultcode>s:Client</faultcode>"
  //       "<faultstring>UPnPDlnaError</faultstring>"
  //       "<detail>"
  //       "<UPnPDlnaError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
  //       "<errorCode>%d</errorCode>"
  //       "<errorDescription>%s</errorDescription>"
  //       "</UPnPDlnaError>"
  //       "</detail>"
  //       "</s:Fault>"
  //       "</s:Body>"
  //       "</s:Envelope>";
  //   sprintf(upnp.getTempBuffer(), tmp, errorCode, error);
  //   return upnp.getTempBuffer();
  // }

  /// If you dont already provid a complete DLNADeviceInfo you can overwrite
  /// this method and add some custom device specific logic to implement a new
  /// device. The MediaRenderer is using this approach!
  virtual void setupServices(DLNADeviceInfo& deviceInfo) {};
};

}  // namespace tiny_dlna