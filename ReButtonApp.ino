#include <Arduino.h>
#include "Config.h"

#include <ReButton.h>
#include "AutoShutdown.h"
#include "Display.h"
#include "Input.h"
#include "Action.h"
#include <AZ3166WiFi.h>
#include <parson.h>
#include <SystemTime.h>

#define LOOP_WAIT_TIME	(10)	// [msec.]
#define POWER_OFF_TIME	(1000)	// [msec.]


static bool DeviceTwinReceived = false;

static void DeviceTwinUpdateCallbackFunc(DEVICE_TWIN_UPDATE_STATE update_state, const unsigned char* payLoad, size_t size)
{
    Serial.println("DeviceTwinUpdateCallbackFunc()");

  JSON_Value* root_value = json_parse_string((const char*)payLoad);
  if (root_value == NULL)
    {
        Serial.println("failure calling json_parse_string");
    return;
    }
    Serial.printf("%s\n", json_serialize_to_string(root_value));

  JSON_Object* root_object = json_value_get_object(root_value);
  int customMessageEnable;
  if (strlen(Config.CustomMessagePropertyName) <= 0)
  {
    customMessageEnable = -1;
  }
  else
  {
    customMessageEnable = json_object_dotget_boolean(root_object, stringformat("desired.%s.value", Config.CustomMessagePropertyName).c_str());
  }

    json_value_free(root_value);

    switch (customMessageEnable)
    {
    case 1:
        Config.CustomMessageEnable = true;
        break;
    case 0:
        Config.CustomMessageEnable = false;
        break;
    }

  DeviceTwinReceived = true;
}

static String stringformat(const char* format, ...)
{
    va_list args;
    char buf[1024];

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    return buf;
}

static String MakeReportJsonString()
{
  JSON_Value* data = json_value_init_object();
  if (strlen(Config.CustomMessagePropertyName) >= 1) json_object_dotset_boolean(json_object(data), stringformat("%s.value", Config.CustomMessagePropertyName).c_str(), Config.CustomMessageEnable);
  //json_object_dotset_number(json_object(data), "batteryVoltage.value", ReButton::ReadPowerSupplyVoltage());
  String jsonString = json_serialize_to_string(data);
  json_value_free(data);

  return jsonString;
}


void setup()
{
	////////////////////
	// Setup auto shutdown

	AutoShutdownBegin(CONFIG_AUTO_SHUTDOWN_TIMEOUT);
	DisplayBegin();

	////////////////////
	// Read CONFIG

	ConfigRead();

	Serial.printf("Firmware version is %s.\n", CONFIG_FIRMWARE_VERSION);

	Serial.println("Parameters:");
	Serial.println("-----");
	ConfigPrint();
	Serial.println("-----");

	if (!ReButton::IsButtonPressed() && ReButton::IsJumperShort())
	{
		Serial.println("Force factory reset.");
		ConfigResetFactorySettings();
		ConfigWrite();
		return;
	}

  ////////////////////
  // Connect Wi-Fi

  SetNTPHost(Config.TimeServer);

  Serial.println("ActionSendMessage() : Wi-Fi - Connecting....");
  if (strlen(Config.WiFiSSID) <= 0 && strlen(Config.WiFiPassword) <= 0)  Serial.println("ActionSendMessage() : Wi-Fi - Failed....");;
    if (WiFi.begin(Config.WiFiSSID, Config.WiFiPassword) != WL_CONNECTED)  Serial.println("ActionSendMessage() : Wi-Fi - Wrong Password or SSID....");;

    while (WiFi.status() != WL_CONNECTED)
    {
    wait_ms(100);
    }

    IPAddress ip = WiFi.localIP();
    Serial.printf("ActionSendMessage() : IP address is %s.\n", ip.get_address());

    ////////////////////
    // Initialize IoTHub client

  ReButtonClient client;
    if (!client.Connect(&DeviceTwinUpdateCallbackFunc))
    {
        Serial.println("ActionSendMessage() : IoT Hub Connection failed");
       // return false;
    }

    ////////////////////
    // Make sure we are connected

  Serial.println("ActionSendMessage() : Wait for connected.");
    while (!client.IsConnected())
    {
    client.DoWork();
    wait_ms(100);
    }
 Serial.println("IoT Hub Client connected.");
   ////////////////////
 // Send message

    String payload = "IoT Button Connected";
    if (!client.SendMessageAsync(payload.c_str()))
  {
    Serial.println("ActionSendMessage() : SendEventAsync failed");
    //return false;
  }
  
   //////////////////
   // Connect to Wifi/IoT Hub ready.
   DisplayColor(DISPLAY_OK);
    ////////////////////
    // Make sure we are received Twin Update

 // Serial.println("ActionSendMessage() : Wait for DeviceTwin received.");
 // while (!DeviceTwinReceived)
 //   {
 //   client.DoWork();
 //   wait_ms(100);
 //   }
 
while (true){

  client.DoWork();
  wait_ms(100);
	////////////////////
	// INPUT
  if (ReButton::IsButtonPressed()){
    Serial.println("Button is pressed.");
    InputBegin();
  } else {
    //Serial.println("Button is NOT pressed.Wait for push button");
    continue;
  }
	
	for (;;)
	{
		InputTask();
		if (!InputIsCapturing()) break;
		DisplayColor(InputToDisplayColor(InputGetCurrentValue()));
		delay(LOOP_WAIT_TIME);
	}
	INPUT_TYPE input = InputGetConfirmValue();
	Serial.printf("Button is %s.\n", InputGetInputString(input));

	////////////////////
	// FLASH

	DisplayStartAction(InputToDisplayColor(input));

	////////////////////
	// ACTION

	ACTION_TYPE action = InputToAction(input);
	Serial.printf("Action is %s.\n", ActionGetActionString(action));

	if (!ActionTaskBlocking(action,client)) return;

	////////////////////
	// FINISH

	Serial.println("Finish.");

	DisplayStartFinish(InputToDisplayColor(input));
	delay(1500);

}
	////////////////////
	// Power off

	//ReButton::PowerSupplyEnable(false);
	//delay(POWER_OFF_TIME);
}

void loop()
{
	////////////////////
	// FINISH (Error)

	for (int i = 0; i < 3; i++)
	{
		DisplayColor(DISPLAY_ERROR);
		delay(200);
		DisplayColor(DISPLAY_OFF);
		delay(200);
	}
	//ReButton::PowerSupplyEnable(true);
	//delay(POWER_OFF_TIME);
}

static DISPLAY_COLOR_TYPE InputToDisplayColor(INPUT_TYPE value)
{
	switch (value)
	{
	case INPUT_SINGLE_CLICK:
		return Config.DisplayColorSingleClick;
	case INPUT_DOUBLE_CLICK:
		return Config.DisplayColorDoubleClick;
	case INPUT_TRIPLE_CLICK:
		return Config.DisplayColorTripleClick;
  case INPUT_FOURTH_CLICK:
    return Config.DisplayColorFourthClick;
  case INPUT_FIFTH_CLICK:
    return Config.DisplayColorFifthClick;
	case INPUT_LONG_PRESS:
		return Config.DisplayColorLongPress;
	case INPUT_SUPER_LONG_PRESS:
		return Config.DisplayColorSuperLongPress;
	case INPUT_ULTRA_LONG_PRESS:
		return Config.DisplayColorUltraLongPress;
	default:
		return DISPLAY_ERROR;
	}
}

static ACTION_TYPE InputToAction(INPUT_TYPE value)
{
	switch (value)
	{
	case INPUT_SINGLE_CLICK:
		return ACTION_1;
	case INPUT_DOUBLE_CLICK:
		return ACTION_2;
	case INPUT_TRIPLE_CLICK:
		return ACTION_3;
  case INPUT_FOURTH_CLICK:
    return ACTION_4;
  case INPUT_FIFTH_CLICK:
    return ACTION_5;
	case INPUT_LONG_PRESS:
		return ACTION_10;
	case INPUT_SUPER_LONG_PRESS:
		return ACTION_11;
	case INPUT_ULTRA_LONG_PRESS:
		return ACTION_AP;
	default:
		return ACTION_NONE;
	}
}
