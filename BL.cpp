#include "BL.h"
#include "RGB.h"
#include "IR.h"
#include "Robot.h"
#include "Mouse.h"
#include "Debug.h"

BLC BL;

elapsedMillis BL_Timer_RX;
elapsedMillis BL_Timer_TX;

void BLC::doRolle(){
    int avail = UART_Pixy.available();
    if(avail>0){
        message = UART_Pixy.readStringUntil('$');
        if(message.length()==16){
            decode(message);
        }
        BL_Timer_RX = 0;
    }

    if(BL_Timer_TX > 10){
        int angle = (digitalReadFast(Start_Port)) ? (int)(round(abs(Ball.Angle)+180)) : 999;
        int dist = (digitalReadFast(Start_Port)) ? (int)round(abs(Ball.Distance)) : 999;
        bool info1 = true; 
        bool info2 = true; 
        int extra = 000; 
        char sendMsg[29]; 
        snprintf(sendMsg, sizeof(sendMsg), "0%03d0%03d0%1d0%1d0%03d$", angle, dist, 0, 0, extra);
        UART_Pixy.print(sendMsg);
        BL_Timer_TX = 0;
    }

    if(BL_Timer_RX > 3000){
        Rolle="N";
    }
}

void BLC::showRolle(){
    if (Rolle=="A"){RGB.write(1,"R");}
    else if (Rolle=="D"){RGB.write(1,"G");}
    else if (Rolle=="N"){RGB.write(1,"B");}
}

void BLC::decode(String message1){
    if(message1.length() < 16) return;
    int angle = message1.substring(1,4).toInt();
    int dist = message1.substring(5,8).toInt();
    bool info1 = message1.substring(9,10).toInt();
    bool info2 = message1.substring(11,12).toInt();
    int extra = message1.substring(13).toInt();

    if((angle == 999 && dist == 999)||(BL_Timer_RX>3000)){SinglePlayer = true;}
    else{
        SinglePlayer = false;
        Ball.Angle_P2 = angle-180;
        Ball.Distance_P2 = dist;
    }

    if(!SinglePlayer){
        if(Ball.Distance_P2 < Ball.Distance){Rolle="D";}
        else {Rolle="A";}
    }
    else{Rolle="N";}
}

void BLC::sendDebug(String msg){
    if(!debugEnabled) return;
    // Prefix "DBG:" wird von der ESP-Bridge erkannt und als Debug-Paket
    // an den Debug-Monitor weitergeleitet (nicht an den Peer-Roboter).
    // '\n' dient als Zeilenende-Erkennung fuer die ESP-Bridge.
    UART_Pixy.print("DBG:");
    UART_Pixy.print(msg);
    UART_Pixy.print('\n');
}

void BLC::setDebugEnabled(bool enabled){
    debugEnabled = enabled;
}

