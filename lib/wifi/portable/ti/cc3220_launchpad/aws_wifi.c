/*
 * Amazon FreeRTOS Wi-Fi for CC3220SF-LAUNCHXL V1.0.3
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file aws_wifi.c
 * @brief Wi-Fi Interface.
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* RTOS port includes. */
#include "pthread.h"
#include "unistd.h"

/* TI Network interface includes. */
#include "network_if.h"

/* Wi-Fi interface includes. */
#include "aws_wifi.h"

/* Wi-Fi config includes. */
#include "aws_wifi_config.h"

/* Secure sockets includes. */
#include "aws_secure_sockets.h"

/**
 * @brief General negative error.
 *
 * Errors generated by logic in this file.
 */
#define wifiGENERAL_INTERNAL_ERROR      -1

/**
 * @brief Binary semaphore for Wi-FI module.
 */
SemaphoreHandle_t xWiFiSemaphoreHandle; /**< Wi-Fi module semaphore. */

/**
 * @brief Wi-Fi initialization status.
 */
static BaseType_t xWIFIInitDone;

/**
 * @brief Wi-Fi device status.
 */
extern volatile unsigned long g_ulStatus;

/**
 * @brief Maximum time to wait in ticks for obtaining the Wi-Fi semaphore
 * before failing the operation.
 */

static const TickType_t xSemaphoreWaitTicks = pdMS_TO_TICKS( wificonfigMAX_SEMAPHORE_WAIT_TIME_MS );

/**
 * @brief Create simple link task, this must be setup before calling any simple link api.
 *
 * @param[in] None.
 *
 * @param[out] eWiFiSuccess if everything succeeds, eWiFiFailure otherwise.
 */
static WIFIReturnCode_t prvCreateSLTask( void );

/**
 * @brief Reset the network processor to an OFF state.
 *
 * @param[in] None.
 *
 * @param[out] eWiFiSuccess if everything succeeds, eWiFiFailure otherwise.
 */
static WIFIReturnCode_t prvResetNetworkCPU( void );

/**
 * @brief Starts Wi-Fi Scan .
 *
 * Starts Wi-Fi scan with specified interval and hidden scan enabled/disabled
 *
 * @param[in] uxIntervalSec Interval in seconds for scanning (min 10 secs / default 10 min)
 *            uxHidden 0 for disabling hidden network scan and 1 to enable it
 *
 * @return eWiFiSuccess if everything succeeds, failure code otherwise.
 */
static WIFIReturnCode_t prvStartScan( uint32_t ulIntervalSec,
                                      uint8_t ucHidden );

/**
 * @brief Stops Wi-Fi Scan .
 *
 * Stops the Wi-Fi scan
 *
 * @param[in] void
 *
 * @return eWiFiSuccess if everything succeeds, failure code otherwise.
 */
static WIFIReturnCode_t prvStopScan( void );

/**
 * @brief Maps the given abstracted security type to TI specific one.
 *
 * @param[in] xSecurity The given abstracted security type.
 *
 * @return Corresponding TI specific security type.
 */
static uint8_t prvConvertSecurityAbstractedToTI( WIFISecurity_t xSecurity );

/**
 * @brief Maps the TI security type to abstracted security type
 *
 * @param[in] ucSecurity The given TI security type
 *
 * @return Corresponding abstracted security type.
 */
static WIFISecurity_t prvConvertSecurityTIToAbstracted( uint8_t ucSecurity );

/**
 * @brief Maps the TI device role type to abstracted role type
 *
 * @param[in] xDeviceMode The given abstracted role type
 *
 * @return Corresponding TI role type.
 */
static SlWlanMode_e prvConvertRoleAbstractedToTI( WIFIDeviceMode_t xDeviceMode );

/**
 * @brief Maps the abstracted role type to TI role type
 *
 * @param[in] xDeviceMode The given TI role type
 *
 * @return Corresponding abstracted role type.
 */
static WIFIDeviceMode_t prvConvertRoleTypeTIToAbstracted( SlWlanMode_e xDeviceMode );

/*-----------------------------------------------------------*/

static uint8_t prvConvertSecurityAbstractedToTI( WIFISecurity_t xSecurity )
{
    uint8_t ucConvertedSecurityType = wificonfigSEC_TYPE_UNKNOWN;

    switch( xSecurity )
    {
        case eWiFiSecurityOpen:
            ucConvertedSecurityType = SL_WLAN_SEC_TYPE_OPEN;
            break;

        case eWiFiSecurityWEP:
            ucConvertedSecurityType = SL_WLAN_SEC_TYPE_WEP;
            break;

        case eWiFiSecurityWPA:
            ucConvertedSecurityType = SL_WLAN_SEC_TYPE_WPA;
            break;

        case eWiFiSecurityWPA2:
            ucConvertedSecurityType = SL_WLAN_SEC_TYPE_WPA_WPA2;
            break;
    }

    return ucConvertedSecurityType;
}

/*-----------------------------------------------------------*/

static WIFISecurity_t prvConvertSecurityTIToAbstracted( uint8_t ucSecurity )
{
    WIFISecurity_t xConvertedSecurityType = eWiFiSecurityNotSupported;

    switch( ucSecurity )
    {
        case SL_WLAN_SEC_TYPE_OPEN:
            xConvertedSecurityType = eWiFiSecurityOpen;
            break;

        case SL_WLAN_SEC_TYPE_WEP:
            xConvertedSecurityType = eWiFiSecurityWEP;
            break;

        /* WPA is deprecated in simple link. */

        /*case SL_WLAN_SEC_TYPE_WPA :
         *  xConvertedSecurityType = eWiFiSecurityWPA;
         *  break;*/

        case SL_WLAN_SEC_TYPE_WPA_WPA2:
            xConvertedSecurityType = eWiFiSecurityWPA2;
            break;

        default:
            break;
    }

    return xConvertedSecurityType;
}

/*-----------------------------------------------------------*/

static SlWlanMode_e prvConvertRoleAbstractedToTI( WIFIDeviceMode_t xDeviceMode )
{
    SlWlanMode_e xConvertedModeType = ROLE_RESERVED;

    switch( xDeviceMode )
    {
        case eWiFiModeStation:
            xConvertedModeType = ROLE_STA;
            break;

        case eWiFiModeAP:
            xConvertedModeType = ROLE_AP;
            break;

        case eWiFiModeP2P:
            xConvertedModeType = ROLE_P2P;
            break;
    }

    return xConvertedModeType;
}

/*-----------------------------------------------------------*/

static WIFIDeviceMode_t prvConvertRoleTypeTIToAbstracted( SlWlanMode_e xDeviceMode )
{
    WIFIDeviceMode_t xConvertedModeType = eWiFiModeNotSupported;

    switch( xDeviceMode )
    {
        case ROLE_STA:
            xConvertedModeType = eWiFiModeStation;
            break;

        case ROLE_AP:
            xConvertedModeType = eWiFiModeAP;
            break;

        case ROLE_P2P:
            xConvertedModeType = eWiFiModeP2P;
            break;
    }

    return xConvertedModeType;
}

/*-----------------------------------------------------------*/

static WIFIReturnCode_t prvCreateSLTask( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    pthread_t spawn_thread = ( pthread_t ) NULL;
    pthread_attr_t pAttrs_spawn;
    struct sched_param priParam;
    int32_t lRetc = 0;

    pthread_attr_init( &pAttrs_spawn );
    priParam.sched_priority = wificonfigSL_TASK_PRIORITY;

    lRetc = pthread_attr_setschedparam( &pAttrs_spawn,
                                        &priParam );

    lRetc |= pthread_attr_setstacksize( &pAttrs_spawn,
                                        wificonfigSL_TASK_STACK_SIZE );

    lRetc |= pthread_attr_setdetachstate( &pAttrs_spawn,
                                          PTHREAD_CREATE_DETACHED );

    lRetc = pthread_create( &spawn_thread,
                            &pAttrs_spawn,
                            sl_Task,
                            NULL );

    if( lRetc == 0 )
    {
        configPRINTF( ( "Simple Link task created\n\r" ) );
        xRetVal = eWiFiSuccess;
    }
    else
    {
        configPRINTF( ( "Could not create Simple Link task\n\r" ) );
        xRetVal = eWiFiFailure;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

static WIFIReturnCode_t prvResetNetworkCPU( void )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int16_t sRetc = 0;

    /* Start the simple link device. sl_start() must be called before sl_Stop() else
       sl_Stop() will fail. */
    sRetc = sl_Start( 0, 0, 0 );

    /* Return codes greater than or equal to zero indicate the mode (station, softAP, or p2p)
     * that the Wi-Fi device has started up in. */
    if( sRetc < 0 )
    {
        /* Handle Error. */
        configPRINTF( ( "\n sl_Start failed\n" ) );
        xRetVal = eWiFiFailure;
    }
    else
    {
        /* Stop the simple link device with a stop timeout in msec to
        * finish any pending transmission/reception*/
        sRetc = sl_Stop( SL_STOP_TIMEOUT );

        if( sRetc != 0 )
        {
            /* Handle Error. */
            configPRINTF( ( "\n sl_Stop failed\n" ) );
            xRetVal = eWiFiFailure;
        }
    }


    return xRetVal;
}

/*-----------------------------------------------------------*/

static WIFIReturnCode_t prvStartScan( uint32_t ulIntervalSec,
                                      uint8_t ucHidden )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    int16_t sRetc = 0;

    uint32_t ulIntervalInSeconds = ulIntervalSec;

    sRetc = sl_WlanPolicySet( SL_WLAN_POLICY_SCAN,
                              SL_WLAN_SCAN_POLICY( 1, ucHidden ),
                              ( uint8_t * ) &ulIntervalInSeconds,
                              sizeof( ulIntervalInSeconds ) );

    if( sRetc == 0 )
    {
        xRetVal = eWiFiSuccess;
    }
    else
    {
        xRetVal = eWiFiFailure;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

static WIFIReturnCode_t prvStopScan( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    int16_t sRetc = -1;

    uint32_t intervalInSeconds = 0;

    sRetc = sl_WlanPolicySet( SL_WLAN_POLICY_SCAN,
                              SL_WLAN_DISABLE_SCAN,
                              ( uint8_t * ) &intervalInSeconds,
                              sizeof( intervalInSeconds ) );

    if( sRetc == 0 )
    {
        xRetVal = eWiFiSuccess;
    }
    else
    {
        xRetVal = eWiFiFailure;
    }

    return xRetVal;
}


/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_On( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    if( xWIFIInitDone == pdFALSE )
    {
        /* This buffer is used to store the semaphore's data structure
         * and therefore must be static. */
        static StaticSemaphore_t xSemaphoreBuffer;

        /* Create the semaphore used to serialize Wi-Fi module operations. */
        xWiFiSemaphoreHandle = xSemaphoreCreateBinaryStatic( &( xSemaphoreBuffer ) );

        if ( xWiFiSemaphoreHandle != NULL )
        {
            /* Initialize semaphore. */
            xSemaphoreGive( xWiFiSemaphoreHandle );

            /*Create simple link task. */
            xRetVal = prvCreateSLTask();

            /* Everything is successful then the one time initialization is done. */
            if ( xRetVal == 0 )
            {
                xWIFIInitDone = pdTRUE;
            }
        }
    }

    /* Reset the network processor. */
    xRetVal = prvResetNetworkCPU();

    if ( xRetVal == eWiFiSuccess )
    {
        /* Reset the state of the machine. */
        Network_IF_ResetMCUStateMachine();

        /* Init the Wi-Fi driver in station mode.*/
        Network_IF_InitDriver( eWiFiModeStation );
    }

    return xRetVal;
}


WIFIReturnCode_t WIFI_Off( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    int16_t sRetCode;

    /* Disconnect from the AP.*/
    Network_IF_DisconnectFromAP();

    /* Stop the simplelink host. */
    sRetCode = sl_Stop(SL_STOP_TIMEOUT);
    if(sRetCode == 0)
    {
        /* Reset the state to uninitialized.*/
        Network_IF_ResetMCUStateMachine();
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_ConnectAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int32_t lRetVal;
    SlWlanSecParams_t xSecurityParams = { 0 };
    char pcSsidCopy[ wificonfigMAX_SSID_LEN + 1 ] = { 0 };

    configASSERT( pxNetworkParams != NULL );
    configASSERT( pxNetworkParams->pcSSID != NULL );

    if ( pxNetworkParams->xSecurity != eWiFiSecurityOpen )
    {
        configASSERT( pxNetworkParams->pcPassword != NULL );
    }

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        if( pxNetworkParams->ucSSIDLength <= wificonfigMAX_SSID_LEN )
        {
            /* Write the input constant SSID to the local copy. A local copy of the
            * Wi-Fi SSID parameter is needed because Network_IF_ConnectAP() will
            * attempt to write to it's input parameter pcSsid. */
            memcpy( pcSsidCopy, pxNetworkParams->pcSSID, pxNetworkParams->ucSSIDLength );

            /* Initialize AP security params. */
            xSecurityParams.Key = ( signed char * ) pxNetworkParams->pcPassword;
            xSecurityParams.KeyLen = pxNetworkParams->ucPasswordLength;

            xSecurityParams.Type = prvConvertSecurityAbstractedToTI( pxNetworkParams->xSecurity );

            if ( xSecurityParams.Type != wificonfigSEC_TYPE_UNKNOWN )
            {
                /* Connect to the Access Point. If the credentials are incorrect this
                * function will ask for an open SSID. */
                lRetVal = Network_IF_ConnectAP( pcSsidCopy,
                                                xSecurityParams );
            }
            else
            {
                lRetVal = wifiGENERAL_INTERNAL_ERROR;
            }
        }
        else
        {
            lRetVal = wifiGENERAL_INTERNAL_ERROR;
        }

        if( lRetVal != 0 )
        {
            configPRINTF( ( "Connection to an AP failed\n\r" ) );
            xRetVal = eWiFiFailure;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Disconnect( void )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    long lRetVal;

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        lRetVal = Network_IF_DisconnectFromAP();

        if( lRetVal == 0 )
        {
            configPRINTF( ( "Wi-Fi Disconnected.\r\n" ) );
        }
        else
        {
            configPRINTF( ( "Wi-Fi already disconnected.\r\n" ) );
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Reset( void )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    long lRetVal;

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        /* Reset The state of the machine.*/
        Network_IF_ResetMCUStateMachine();

        /* Disconnect if already connected, stop the sl driver.*/
        Network_IF_DeInitDriver();

        /* Restart the driver in station mode. */
        lRetVal = Network_IF_InitDriver( eWiFiModeStation );

        if( lRetVal != 0 )
        {
            configPRINTF( ( "Failed to start SimpleLink Device\n\r", lRetVal ) );
            xRetVal = eWiFiFailure;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}


/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_SetMode( WIFIDeviceMode_t xDeviceMode )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    long lRetVal;
    long lDeviceMode;

    lDeviceMode = prvConvertRoleAbstractedToTI( xDeviceMode );

    if ( lDeviceMode == ROLE_RESERVED )
    {
        xRetVal = eWiFiFailure;
    }
    else
    {
        /* Try to acquire the semaphore. */
        if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
        {
            /* Set the simple link device.*/
            sl_WlanSetMode( lDeviceMode );

            /* Reset the simple link device.*/
            sl_Stop( 0xFF );
            lRetVal = sl_Start( 0, 0, 0 );

            /* Check if the desired mode is set.*/
            if( lRetVal != lDeviceMode )
            {
                xRetVal = eWiFiFailure;
            }
            /* If the desired rol is AP, then keep trying. */
            else if( lRetVal == ROLE_AP )
            {
                while( !IS_IP_ACQUIRED( g_ulStatus ) )
                {
                    usleep( 1000 );
                }
            }

            /* Return the semaphore. */
            xSemaphoreGive( xWiFiSemaphoreHandle );
        }
        else
        {
            xRetVal = eWiFiTimeout;
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/
WIFIReturnCode_t WIFI_GetMode( WIFIDeviceMode_t * pxDeviceMode )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    uint16_t usLen;
    SlWlanConnStatusParam_t xWlanConnectInfo;

    configASSERT( pxDeviceMode != NULL );

    usLen = sizeof( SlWlanConnStatusParam_t );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        sl_WlanGet( SL_WLAN_CONNECTION_INFO,
                    NULL,
                    &usLen,
                    ( uint8_t * ) &xWlanConnectInfo );

        *pxDeviceMode = prvConvertRoleTypeTIToAbstracted( ( SlWlanMode_e ) xWlanConnectInfo.Mode );

        if ( *pxDeviceMode == eWiFiModeNotSupported )
        {
            xRetVal = eWiFiFailure;
        }
        else
        {
            xRetVal = eWiFiSuccess;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}
/*-----------------------------------------------------------*/


WIFIReturnCode_t WIFI_NetworkAdd( const WIFINetworkProfile_t * const pxNetworkProfile,
                                  uint16_t * pusIndex )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    int16_t sRetcode;

    SlWlanSecParams_t xSecurityParams = { 0 };

    configASSERT( pxNetworkProfile != NULL );
    configASSERT( pusIndex != NULL );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        /* Initialize AP security params. */
        xSecurityParams.Key = ( signed char * ) pxNetworkProfile->cPassword;
        xSecurityParams.KeyLen = pxNetworkProfile->ucPasswordLength;
        xSecurityParams.Type = prvConvertSecurityAbstractedToTI( pxNetworkProfile->xSecurity );

        if ( xSecurityParams.Type != wificonfigSEC_TYPE_UNKNOWN )
        {
            sRetcode = sl_WlanProfileAdd( ( signed char * ) pxNetworkProfile->cSSID,
                                          pxNetworkProfile->ucSSIDLength,
                                          pxNetworkProfile->ucBSSID,
                                          &xSecurityParams,
                                          NULL,
                                          NULL,
                                          NULL );
        }
        else
        {
            sRetcode = wifiGENERAL_INTERNAL_ERROR;
        }

        /* sl_WlanProfileAdd() will return a zero or positive number on success see wlan.h */
        if( sRetcode < 0 )
        {
            configPRINTF( ( "Network profile add failed\n\r" ) );
            xRetVal = eWiFiFailure;
        }
        else
        {
            configPRINTF( ( "Network profile stored at index %d.\r\n", sRetcode ) );
            xRetVal = eWiFiSuccess;
            *pusIndex = ( uint16_t ) sRetcode;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_NetworkGet( WIFINetworkProfile_t * pxNetworkProfile,
                                  uint16_t usIndex )

{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    int16_t sRetCode;
    int16_t sSSIDLength;
    SlWlanSecParams_t xSecurityParams;
    SlWlanGetSecParamsExt_t pxSecExtParams;
    unsigned long ulPrio;

    configASSERT( pxNetworkProfile != NULL );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        sRetCode = sl_WlanProfileGet( usIndex,
                                      ( signed char * ) pxNetworkProfile->cSSID,
                                      &sSSIDLength,
                                      pxNetworkProfile->ucBSSID,
                                      &xSecurityParams,
                                      &pxSecExtParams,
                                      &ulPrio );

        /* See wlan.h for information about possible return codes from sl_WlanProfileGet(). */
        if( sRetCode < 0 )
        {
            xRetVal = eWiFiFailure;
        }
        else
        {
            pxNetworkProfile->ucSSIDLength = sSSIDLength;
            pxNetworkProfile->xSecurity = prvConvertSecurityTIToAbstracted( xSecurityParams.Type );
            pxNetworkProfile->ucPasswordLength = 0; /*password not returned.*/
            xRetVal = eWiFiSuccess;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_NetworkDelete( uint16_t usIndex )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    int16_t sRetCode;

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        sRetCode = sl_WlanProfileDel( usIndex );

        if( sRetCode != 0 )
        {
            configPRINTF( ( "Network profile delete failed\n\r" ) );
            xRetVal = eWiFiFailure;
        }
        else
        {
            configPRINTF( ( "Network profile deleted at index %d.\r\n", usIndex ) );
            xRetVal = eWiFiSuccess;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Ping( uint8_t * pucIPAddr,
                            uint16_t usCount,
                            uint32_t ulIntervalMS )
{
    WIFIReturnCode_t xRetVal = eWiFiNotSupported;

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetIP( uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int16_t sRetCode;

    configASSERT( pucIPAddr != NULL );

    unsigned long ulDestinationIP = 0;
    unsigned long ulSubMask = 0;
    unsigned long ulDefGateway = 0;
    unsigned long ulDns = 0;

    /* Get IP address. */
    sRetCode = ( int16_t ) Network_IF_IpConfigGet( &ulDestinationIP,
                                                   &ulSubMask,
                                                   &ulDefGateway,
                                                   &ulDns );

    if( sRetCode != 0 )
    {
        xRetVal = eWiFiFailure;
    }
    else
    {
        /*fill the return buffer.*/
        *( pucIPAddr ) = SL_IPV4_BYTE( ulDestinationIP, 3 );
        *( pucIPAddr + 1 ) = SL_IPV4_BYTE( ulDestinationIP, 2 );
        *( pucIPAddr + 2 ) = SL_IPV4_BYTE( ulDestinationIP, 1 );
        *( pucIPAddr + 3 ) = SL_IPV4_BYTE( ulDestinationIP, 0 );
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetMAC( uint8_t * pucMac )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int16_t sRetCode;
    uint8_t ucMacAddressVal[ wificonfigMAX_BSSID_LEN ];
    uint16_t usMacAddressLen = wificonfigMAX_BSSID_LEN;
    uint16_t usConfigOpt = 0;

    configASSERT( pucMac != NULL );

    sRetCode = sl_NetCfgGet( SL_NETCFG_MAC_ADDRESS_GET,
                             &usConfigOpt,
                             &usMacAddressLen,
                             ( uint8_t * ) ucMacAddressVal );

    if( sRetCode != 0 )
    {
        configPRINTF( ( "Failed to get MAC address .\r\n" ) );
        xRetVal = eWiFiFailure;
    }
    else
    {
        memcpy( pucMac, ucMacAddressVal, wificonfigMAX_BSSID_LEN );
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetHostIP( char * pxHost,
                                 uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    uint32_t ulDestinationIP;

    configASSERT( pxHost != NULL );
    configASSERT( pucIPAddr != NULL );

    ulDestinationIP = SOCKETS_GetHostByName( pxHost );

    if( ulDestinationIP == 0 )
    {
        xRetVal = eWiFiFailure;
    }
    else
    {
        /*fill the return buffer.*/
        *( pucIPAddr ) = SL_IPV4_BYTE( ulDestinationIP, 3 );
        *( pucIPAddr + 1 ) = SL_IPV4_BYTE( ulDestinationIP, 2 );
        *( pucIPAddr + 2 ) = SL_IPV4_BYTE( ulDestinationIP, 1 );
        *( pucIPAddr + 3 ) = SL_IPV4_BYTE( ulDestinationIP, 0 );
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Scan( WIFIScanResult_t * pxBuffer,
                            uint8_t ucNumNetworks )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    SlWlanNetworkEntry_t * pxNetEntries;
    uint8_t i;

    configASSERT( pxBuffer != NULL );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        /*Start Wi-Fi scan with interval period and hidden scan enabled.*/
        xRetVal = prvStartScan( wificonfigSCAN_INTERVAL_SEC,
                                wificonfigSCAN_HIDDEN_CHANNEL );

        /* If the scan was sucessful. */
        if( xRetVal == eWiFiSuccess )
        {
            /*Scan timeout. */
            vTaskDelay( wificonfigSCAN_DURATION_SEC );

            /*Stop Wi-Fi scan. */
            xRetVal = prvStopScan();

        }

        /* If the scan successfully stopped. */
        if( xRetVal == eWiFiSuccess )
        {
            /* TI's compiler has a bug if you use array with variable length on stack, it will malloc without freeing it */
            pxNetEntries = pvPortMalloc( sizeof( SlWlanNetworkEntry_t ) * ucNumNetworks );

            if( pxNetEntries != NULL )
            {
                /*Get the scan results. */
                sl_WlanGetNetworkList( 0,
                                    ucNumNetworks,
                                    &pxNetEntries[ 0 ] );

                for( i = 0 ; i < ucNumNetworks ; i++ )
                {
                    strncpy( ( char * ) pxBuffer[ i ].cSSID,
                            ( char * ) pxNetEntries[ i ].Ssid,
                            wificonfigMAX_SSID_LEN );
                    pxBuffer[ i ].cSSID[ wificonfigMAX_SSID_LEN ] = '\0';

                    strncpy( ( char * ) pxBuffer[ i ].ucBSSID,
                            ( char * ) pxNetEntries[ i ].Bssid,
                            wificonfigMAX_BSSID_LEN );

                    pxBuffer[ i ].cChannel = pxNetEntries[ i ].Channel;
                    pxBuffer[ i ].cRSSI = pxNetEntries[ i ].Rssi;

                    /* security types as supported by simplelink . */
                    pxBuffer[ i ].xSecurity = prvConvertSecurityTIToAbstracted( SL_WLAN_SCAN_RESULT_SEC_TYPE_BITMAP( pxNetEntries[ i ].SecurityInfo ) );

                    pxBuffer[ i ].ucHidden = SL_WLAN_SCAN_RESULT_HIDDEN_SSID( pxNetEntries[ i ].SecurityInfo );
                }

                vPortFree( pxNetEntries );
            }
            else
            {
                xRetVal = eWiFiFailure;
            }
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_StartAP( void )
{
    WIFIReturnCode_t xRetVal = eWiFiNotSupported;

    /*Use set mode to activate access point.*/
    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_StopAP( void )
{
    WIFIReturnCode_t xRetVal = eWiFiNotSupported;

    return xRetVal;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_ConfigureAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    uint8_t ucSSIDStr[ wificonfigMAX_SSID_LEN + 1 ];
    uint8_t ucPassStr[ wificonfigMAX_PASSPHRASE_LEN + 1 ];
    int16_t sRetCode;
    uint8_t ucChannel;
    uint8_t ucSecurityType;

    configASSERT( pxNetworkParams != NULL );
    configASSERT( pxNetworkParams->pcSSID != NULL );

    if ( pxNetworkParams->xSecurity != eWiFiSecurityOpen )
    {
        configASSERT( pxNetworkParams->pcPassword != NULL );
    }

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        xRetVal = eWiFiSuccess;

        if( pxNetworkParams->ucSSIDLength > wificonfigMAX_SSID_LEN )
        {
            /* Set a negative error code to fall out and give up the semaphore. */
            sRetCode = wifiGENERAL_INTERNAL_ERROR;
        }
        else
        {
            /*Set Access point SSID.*/
            memcpy( ucSSIDStr, pxNetworkParams->pcSSID, pxNetworkParams->ucSSIDLength );
            ucSSIDStr[ pxNetworkParams->ucSSIDLength ] = '\0';
            sRetCode = sl_WlanSet( SL_WLAN_CFG_AP_ID,
                                SL_WLAN_AP_OPT_SSID,
                                pxNetworkParams->ucSSIDLength,
                                ucSSIDStr );
        }

        /* Print error, if there is one. */
        if( sRetCode != 0 )
        {
            configPRINTF( ( "Failed to set Access point SSID. Error code: %d\r\n", sRetCode ) );
            xRetVal = eWiFiFailure;
        }

        if ( sRetCode == 0 )
        {
            /*Set Access point Channel.*/
            ucChannel = pxNetworkParams->cChannel;
            sRetCode = sl_WlanSet( SL_WLAN_CFG_AP_ID,
                                   SL_WLAN_AP_OPT_CHANNEL,
                                   1,
                                   ( uint8_t * ) &ucChannel );
            /* Print error, if there is one. */
            if( sRetCode != 0 )
            {
                configPRINTF( ( "Failed to set Access point channel. Error code: %d\r\n", sRetCode ) );
                xRetVal = eWiFiFailure;
            }
        }

        if ( sRetCode == 0 )
        {
            /*Set Access point security type.*/
            ucSecurityType = prvConvertSecurityAbstractedToTI( pxNetworkParams->xSecurity );
            sRetCode = sl_WlanSet( SL_WLAN_CFG_AP_ID,
                                   SL_WLAN_AP_OPT_SECURITY_TYPE,
                                   1,
                                   ( uint8_t * ) &ucSecurityType );
            /* Print error, if there is one. */
            if( sRetCode != 0 )
            {
                configPRINTF( ( "Failed to set Access point security type. Error code: %d\r\n", sRetCode ) );
                xRetVal = eWiFiFailure;
            }
        }

        if ( sRetCode == 0 )
        {
            if ( pxNetworkParams->xSecurity != eWiFiSecurityOpen )
            {

                if( pxNetworkParams->ucPasswordLength > wificonfigMAX_PASSPHRASE_LEN )
                {
                    /* Set a negative error code to fall out and give up the semaphore. */
                    sRetCode = wifiGENERAL_INTERNAL_ERROR;
                }
                else
                {
                    /*Set Access point password.*/
                    memcpy( ucPassStr, pxNetworkParams->pcPassword, pxNetworkParams->ucPasswordLength );
                    ucPassStr[ pxNetworkParams->ucPasswordLength ] = '\0';

                    sRetCode = sl_WlanSet( SL_WLAN_CFG_AP_ID,
                                        SL_WLAN_AP_OPT_PASSWORD,
                                        pxNetworkParams->ucPasswordLength,
                                        ( uint8_t * ) ucPassStr );
                }

                if( sRetCode != 0 )
                {
                    configPRINTF( ( "Failed to set Access point password.\r\n" ) );
                    xRetVal = eWiFiFailure;
                }
            }
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_SetPMMode( WIFIPMMode_t xPMModeType,
                                 const void * pvOptionValue )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int16_t sRetCode = 0;

    configASSERT( pvOptionValue != NULL );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        switch( xPMModeType )
        {
            case eWiFiPMNormal:
                sRetCode = sl_WlanPolicySet( SL_WLAN_POLICY_PM,
                                             SL_WLAN_NORMAL_POLICY,
                                             NULL,
                                             0 );
                break;

            case eWiFiPMLowPower:
                sRetCode = sl_WlanPolicySet( SL_WLAN_POLICY_PM,
                                             SL_WLAN_LOW_POWER_POLICY,
                                             NULL,
                                             0 );
                break;

            case eWiFiPMAlwaysOn:
                sRetCode = sl_WlanPolicySet( SL_WLAN_POLICY_PM,
                                             SL_WLAN_ALWAYS_ON_POLICY,
                                             NULL,
                                             0 );
                break;

            default:
                xRetVal = eWiFiNotSupported;
                break;
        }

        if( sRetCode != 0 )
        {
            configPRINTF( ( "Failed to set power management mode.\r\n" ) );
            xRetVal = eWiFiFailure;
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetPMMode( WIFIPMMode_t * pxPMModeType,
                                 void * pvOptionValue )
{
    WIFIReturnCode_t xRetVal = eWiFiSuccess;
    int16_t sRetCode;
    uint8_t ucPolicy = 0;
    uint8_t uclength;
    SlWlanPmPolicyParams_t xPmPolicyParams;

    configASSERT( pxPMModeType != NULL );
    configASSERT( pvOptionValue != NULL );

    uclength = sizeof( xPmPolicyParams );

    /* Try to acquire the semaphore. */
    if( xSemaphoreTake( xWiFiSemaphoreHandle, xSemaphoreWaitTicks ) == pdTRUE )
    {
        sRetCode = sl_WlanPolicyGet( SL_WLAN_POLICY_PM,
                                     &ucPolicy,
                                     ( uint8_t * ) &xPmPolicyParams,
                                     ( uint8_t * ) &uclength );

        if( sRetCode != 0 )
        {
            configPRINTF( ( "Failed to get power management mode.\r\n" ) );
            xRetVal = eWiFiFailure;
        }
        else
        {
            switch( ucPolicy )
            {
                case SL_WLAN_NORMAL_POLICY:
                    *pxPMModeType = eWiFiPMNormal;
                    break;

                case SL_WLAN_LOW_POWER_POLICY:
                    *pxPMModeType = eWiFiPMLowPower;
                    break;

                case SL_WLAN_ALWAYS_ON_POLICY:
                    *pxPMModeType = eWiFiPMAlwaysOn;
                    break;

                default:
                    *pxPMModeType = eWiFiPMNotSupported;
                    break;
            }
        }

        /* Return the semaphore. */
        xSemaphoreGive( xWiFiSemaphoreHandle );
    }
    else
    {
        xRetVal = eWiFiTimeout;
    }

    return xRetVal;
}
/*-----------------------------------------------------------*/

BaseType_t WIFI_IsConnected( void )
{
    BaseType_t xIsConnected = pdFALSE;

    if ( IS_CONNECTED( Network_IF_CurrentMCUState() ) ){
        xIsConnected = pdTRUE;
    }

    return xIsConnected;
}

WIFIReturnCode_t WIFI_RegisterNetworkStateChangeEventCallback( IotNetworkStateChangeEventCallback_t xCallback  )
{
    /** Needs to implement dispatching network state change events **/
    return eWiFiNotSupported;
}
