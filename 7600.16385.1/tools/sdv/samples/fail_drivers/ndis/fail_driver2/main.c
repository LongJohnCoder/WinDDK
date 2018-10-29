#include "mp.h"

NDIS_HANDLE     NdisMiniportDriverHandle = NULL;
NDIS_HANDLE     MiniportDriverContext = NULL;


NDIS_STATUS DriverEntry(
    IN  PDRIVER_OBJECT   DriverObject,
    IN  PUNICODE_STRING  RegistryPath
    )
/*++
Routine Description:

Arguments:

    DriverObject    -   pointer to the driver object
    RegistryPath    -   pointer to the driver registry path
     
Return Value:
    
    NDIS_STATUS - the value returned by NdisMRegisterMiniport 
    
--*/
{
    NDIS_STATUS                         Status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MPChar;
        
    //
    // Fill in the Miniport characteristics structure with the version numbers 
    // and the entry points for driver-supplied MiniportXxx 
    //
    NdisZeroMemory(&MPChar, sizeof(MPChar));

    MPChar.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS,
    MPChar.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    MPChar.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;

    MPChar.MajorNdisVersion             = MP_NDIS_MAJOR_VERSION;
    MPChar.MinorNdisVersion             = MP_NDIS_MINOR_VERSION;
    MPChar.MajorDriverVersion           = MP_NDIS_MAJOR_VERSION;
    MPChar.MinorDriverVersion           = MP_NDIS_MINOR_VERSION;

    MPChar.SetOptionsHandler            = MPSetOptions;
    
    MPChar.InitializeHandlerEx          = MPInitialize;
    MPChar.HaltHandlerEx                = MPHalt;

    MPChar.UnloadHandler                = MPUnload,
    
    MPChar.PauseHandler                 = MPPause;      
    MPChar.RestartHandler               = MPRestart;    
    MPChar.OidRequestHandler            = MPOidRequest;    
    MPChar.SendNetBufferListsHandler    = MPSendNetBufferLists;
    MPChar.ReturnNetBufferListsHandler  = MPReturnNetBufferLists;
    MPChar.CancelSendHandler            = MPCancelSendNetBufferLists;
    MPChar.DevicePnPEventNotifyHandler  = MPPnPEventNotify;
    MPChar.ShutdownHandlerEx            = MPShutdown;
    MPChar.CheckForHangHandlerEx        = MPCheckForHang;
    MPChar.ResetHandlerEx               = MPReset;
    MPChar.CancelOidRequestHandler      = MPCancelOidRequest;
    
    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         (PNDIS_HANDLE)MiniportDriverContext,
                                         &MPChar,
                                         &NdisMiniportDriverHandle);
    return Status;
}


NDIS_STATUS
MPSetOptions(
    IN NDIS_HANDLE  NdisMiniportDriverHandle,
    IN NDIS_HANDLE  MiniportDriverContext
    )
{
    UNREFERENCED_PARAMETER(NdisMiniportDriverHandle);
    UNREFERENCED_PARAMETER(MiniportDriverContext);
    
    //MINIPORT_SET_OPTIONS
    
    return (NDIS_STATUS_SUCCESS);
}

NDIS_STATUS 
MPInitialize(
    IN  NDIS_HANDLE                        MiniportAdapterHandle,
    IN  NDIS_HANDLE                        MiniportDriverContext,
    IN  PNDIS_MINIPORT_INIT_PARAMETERS     MiniportInitParameters
    )
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    PMP_ADAPTER     pAdapter = NULL;
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES   RegistrationAttributes;
    NDIS_TIMER_CHARACTERISTICS                      Timer;
    
    NDIS_PHYSICAL_ADDRESS physicalAddress; 
    PVOID pSharedMemory;              

    //MINIPORT_INITIALIZE

    do
    {
        pAdapter = (PMP_ADAPTER)NdisAllocateMemoryWithTagPriority(
            MiniportAdapterHandle,
            sizeof(MP_ADAPTER),
            NIC_TAG,
            LowPoolPriority
            );
        __sdv_save_adapter_context((PVOID*)&pAdapter);
        if (pAdapter == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            break;
        }

        NdisZeroMemory(pAdapter, sizeof(MP_ADAPTER));
        
        pAdapter->AdapterHandle = MiniportAdapterHandle;
        pAdapter->PauseState = NicPaused;
        //
        // setting registration attributes
        //
        RegistrationAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
        RegistrationAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
        RegistrationAttributes.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);

        RegistrationAttributes.MiniportAdapterContext = (NDIS_HANDLE)pAdapter;
        RegistrationAttributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE | 
                                                NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER;
        
        RegistrationAttributes.CheckForHangTimeInSeconds = 2;
        RegistrationAttributes.InterfaceType = NIC_INTERFACE_TYPE;

        Status = NdisMSetMiniportAttributes(MiniportAdapterHandle,
                                            (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegistrationAttributes);

        if (Status != NDIS_STATUS_SUCCESS)
        {
            break;
        }

        NdisAllocateSpinLock(&pAdapter->Lock);

        // Allocate Adapter memory
        Status = NICAllocAdapterMemory(pAdapter);
        if (Status != NDIS_STATUS_SUCCESS) 
        {
            break;
        }
        pAdapter->AdapetrMemoryAllocated = TRUE;

        //
        // Register the interrupt
        //
        Status = MpRegisterInterrupt(pAdapter);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            break;
        }
        pAdapter->InterruptRegistered = TRUE;

        //
        // Register a timer
        //
        Status = MpQueueTimer(pAdapter);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            break;
        }
        pAdapter->TimerRegistered = TRUE;

        // Allocate some shared memory
        NdisMAllocateSharedMemory (MiniportAdapterHandle, 100, TRUE, &pSharedMemory, &physicalAddress);

        // ------------------------------------------------------------------------------------
        // SDV - Injected Defect - Rule: WriteErrorLog
        // Note: Commenting the line below the failure won't be logged.
        //        Logging the failure is a best practice that you should follow.
        // ------------------------------------------------------------------------------------ 
        // NdisWriteErrorLogEntry(
        //     MiniportAdapterHandle,
        //     NDIS_ERROR_CODE_OUT_OF_RESOURCES,
        //     1,
        //     0); 
        // ------------------------------------------------------------------------------------
         
	      
        pAdapter->PNPState = NicStarted;
    } while (FALSE);

    if (pAdapter && (Status != NDIS_STATUS_SUCCESS))
    {
        //
        // Undo everything if it failed
        //
        MPHalt(pAdapter, NdisHaltDeviceInitializationFailed);
    }

    return Status;
}


NDIS_STATUS 
MPPause(
    IN  NDIS_HANDLE                         MiniportAdapterContext,    
    IN  PNDIS_MINIPORT_PAUSE_PARAMETERS     MiniportPauseParameters
    )
{
    PMP_ADAPTER         pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
    NDIS_STATUS         Status = NDIS_STATUS_PENDING;

    //MINIPORT_PAUSE
 
    // ------------------------------------------------------------------------------------
    // SDV - Injected Defect - Rule: SpinLockBalanced
    // Note: Commenting the line below, the driver will try to release the SpinLock
    //        without first acquiring it. 
    // ------------------------------------------------------------------------------------ 
    // NdisAcquireSpinLock(&pAdapter->Lock);
    // ------------------------------------------------------------------------------------
    pAdapter->PauseState = NicPausing;

    //
    // If there are not pending operations
    //
    if (pAdapter->RefCount == 0)
    {
        Status = NDIS_STATUS_SUCCESS;
        pAdapter->PauseState = NicPaused;            
    }
    NdisReleaseSpinLock(&pAdapter->Lock);

    return Status;
}
        
    
NDIS_STATUS 
MPRestart(
    IN  NDIS_HANDLE                         MiniportAdapterContext,    
    IN  PNDIS_MINIPORT_RESTART_PARAMETERS   MiniportRestartParameters
    )
{
    PMP_ADAPTER                  pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

    //MINIPORT_RESTART
    NdisAcquireSpinLock(&pAdapter->Lock);
    pAdapter->PauseState = NicRunning;
    NdisReleaseSpinLock(&pAdapter->Lock);    

    return NDIS_STATUS_SUCCESS;
}
 
  
BOOLEAN 
MPCheckForHang(
    IN  NDIS_HANDLE     MiniportAdapterContext
    )
{
    PMP_ADAPTER         pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
    BOOLEAN             NeedsReset = FALSE;

    //MINIPORT_CHECK_FOR_HANG

    NdisAcquireSpinLock(&pAdapter->Lock);
    if (pAdapter->LastRefCount <= pAdapter->RefCount)
    {
        //
        // We claim we need a reset
        //
        NeedsReset = TRUE;
    }
    pAdapter->LastRefCount = pAdapter->RefCount;
    NdisReleaseSpinLock(&pAdapter->Lock);    

    return NeedsReset;
}


VOID MPHalt(
    IN  NDIS_HANDLE             MiniportAdapterContext,
    IN  NDIS_HALT_ACTION        HaltAction
    )

/*++

Routine Description:
    
    MiniportHalt handler
    
Arguments:

    MiniportAdapterContext  Pointer to our adapter
    HaltAction              The reason adapter is being halted

Return Value:

    None
    
--*/
{
    PMP_ADAPTER     pAdapter = (PMP_ADAPTER) MiniportAdapterContext;

    // MINIPORT_HALT

    NdisAcquireSpinLock(&pAdapter->Lock);
    pAdapter->PNPState = NicHalted;
    NdisReleaseSpinLock(&pAdapter->Lock);    

    //
    // Deregister interrupt as early as possible
    //

    if (pAdapter->InterruptRegistered)
    {
        NdisMDeregisterInterruptEx(pAdapter->InterruptHandle);
        pAdapter->InterruptRegistered = FALSE;
    }

    if (pAdapter->TimerRegistered)
    {
        // ------------------------------------------------------------------------------------
        // SDV - Injected Defect - Rule: PeriociTimer
        // Note: Acquiring the SpinLock will raise the IRQL to disptach, 
        //        causing an error while trying to cancel the periodic timer
        //        that we set in MpQueueTimer.  
        // ------------------------------------------------------------------------------------
        NdisAcquireSpinLock(&pAdapter->Lock);     
        NdisCancelTimerObject (pAdapter->TimerHandle);
        NdisReleaseSpinLock(&pAdapter->Lock);
        // ------------------------------------------------------------------------------------
        pAdapter->TimerRegistered = FALSE;
    }

    if (pAdapter->AdapetrMemoryAllocated) 
    {
        NdisMDeregisterScatterGatherDma(pAdapter->AdapterHandle);
        pAdapter->AdapetrMemoryAllocated = FALSE;
    }

    NdisFreeSpinLock(&(pAdapter->Lock));
    
    NdisFreeMemory(pAdapter, 0, 0);
    
}

NDIS_STATUS 
MPReset(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    OUT PBOOLEAN        AddressingReset
    )
{
    PMP_ADAPTER         pAdapter = (PMP_ADAPTER) MiniportAdapterContext;

    *AddressingReset = TRUE;

    //MINIPORT_RESET
    NdisAcquireSpinLock(&pAdapter->Lock);
    pAdapter->PNPState = NicInReset;
    pAdapter->RefCount = 0;
    pAdapter->PNPState = NicStarted;
    NdisReleaseSpinLock(&pAdapter->Lock);
    
    return NDIS_STATUS_SUCCESS;
}

VOID 
MPReturnNetBufferLists(
    IN  NDIS_HANDLE         MiniportAdapterContext,
    IN  PNET_BUFFER_LIST    NetBufferLists,
    IN  ULONG               ReturnFlags
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
    
    //MINIPORT_RETURN_NET_BUFFER_LISTS
}


VOID 
MPSendNetBufferLists(
    IN  NDIS_HANDLE         MiniportAdapterContext,
    IN  PNET_BUFFER_LIST    NetBufferList,
    IN  NDIS_PORT_NUMBER    PortNumber,
    IN  ULONG               SendFlags
    )
{
    PMP_ADAPTER         pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
    PNET_BUFFER_LIST    CurrNetBufferList;
    PNET_BUFFER_LIST    NextNetBufferList;
    ULONG               SendCompleteFlags = 0;
    NDIS_HANDLE         NdisIoWorkItemHandle = NULL;
    
    //MINIPORT_SEND_NET_BUFFER_LISTS
    
    for (CurrNetBufferList = NetBufferList;
             CurrNetBufferList != NULL;
             CurrNetBufferList = NextNetBufferList)
    {
        NextNetBufferList = NET_BUFFER_LIST_NEXT_NBL(CurrNetBufferList);
        NET_BUFFER_LIST_STATUS(CurrNetBufferList) = NDIS_STATUS_SUCCESS;
    }

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
    {
        NDIS_SET_SEND_COMPLETE_FLAG(SendCompleteFlags, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
    }

    NdisIoWorkItemHandle = NdisAllocateIoWorkItem(pAdapter->AdapterHandle);
        
    NdisQueueIoWorkItem(NdisIoWorkItemHandle,
                            MPQueuedWorkItem,
                            NULL);


    NdisMSendNetBufferListsComplete(
        pAdapter->AdapterHandle,
        NetBufferList,
        SendCompleteFlags
        );
}

VOID MPShutdown(
    IN  NDIS_HANDLE             MiniportAdapterContext,
    IN  NDIS_SHUTDOWN_ACTION    ShutdownAction
    )
{
    PMP_ADAPTER     pAdapter = (PMP_ADAPTER)MiniportAdapterContext;

    // MINIPORT_SHUTDOWN

    UNREFERENCED_PARAMETER(ShutdownAction);

    pAdapter->PNPState = NicShutdown;
}
 
BOOLEAN 
MPIsr(
    IN  NDIS_HANDLE     MiniportInterruptContext,
    OUT PBOOLEAN        QueueMiniportInterruptDpcHandler,
    OUT PULONG          TargetProcessors
    )
 {
    PMP_ADAPTER  pAdapter = (PMP_ADAPTER)MiniportInterruptContext;
    BOOLEAN      InterruptRecognized = FALSE;

    //MINIPORT_ISR

    NdisAcquireSpinLock(&pAdapter->Lock);
    if ((pAdapter->PNPState == NicStarted) || (pAdapter->PNPState == NicInReset))
    {
        // Yeah, I know I am accepting all interrupts. This will put the 
        // machine into unstable state, but this is just a test driver
        InterruptRecognized = TRUE;
    }
    NdisReleaseSpinLock(&pAdapter->Lock);

    return InterruptRecognized;
}



VOID 
MPCancelSendNetBufferLists(
    IN  NDIS_HANDLE     MiniportAdapterContext,
    IN  PVOID           CancelId
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
    
    //MINIPORT_CANCEL_SEND

}

VOID 
MPPnPEventNotify(
    IN  NDIS_HANDLE             MiniportAdapterContext,
    IN  PNET_DEVICE_PNP_EVENT   NetDevicePnPEvent
    )
{
    PMP_ADAPTER     Adapter = (PMP_ADAPTER)MiniportAdapterContext;
    NDIS_DEVICE_PNP_EVENT   PnPEvent = NetDevicePnPEvent->DevicePnPEvent;
    PVOID                   InformationBuffer = NetDevicePnPEvent->InformationBuffer;
    ULONG                   InformationBufferLength = NetDevicePnPEvent->InformationBufferLength;

    //
    // Turn off the warings.
    //
    UNREFERENCED_PARAMETER(InformationBuffer);
    UNREFERENCED_PARAMETER(InformationBufferLength);
    UNREFERENCED_PARAMETER(Adapter);
    
    //MINIPORT_DEVICE_PNP_EVENT_NOTIFY


    switch (PnPEvent)
    {
        case NdisDevicePnPEventQueryRemoved:
            break;

        case NdisDevicePnPEventRemoved:
            break;       

        case NdisDevicePnPEventSurpriseRemoved:
            break;

        case NdisDevicePnPEventQueryStopped:
            break;

        case NdisDevicePnPEventStopped:
            break;      
            
        case NdisDevicePnPEventPowerProfileChanged:
            break;      
            
        default:
            break;         
    }
}


VOID 
MPUnload(
    IN  PDRIVER_OBJECT  DriverObject
    )
{
    //
    // Deregister Miniport driver
    //
    NdisMDeregisterMiniportDriver(NdisMiniportDriverHandle);
    //MINIPORT_UNLOAD
}

BOOLEAN
NICQueryGenStatistics(    
    IN  NDIS_HANDLE        MiniportAdapterContext,
    IN  PNDIS_OID_REQUEST  NdisRequest
    )
{
    // Do something to handle the OID correctly
    
    // ------------------------------------------------------------------------------------
    // SDV - Injected Defect - Rule: DoubleComplete
    // Note: If this function is called from MPOidRequest and 
    //          MPOidRequest is returning the vaule returned from the function itself, 
    //          NdisMOidRquestcomplete will be called and MPOidRequest will return true, 
    //          completeing the request twice.  
    // ------------------------------------------------------------------------------------
    NdisMOidRequestComplete(MiniportAdapterContext,
                            NdisRequest,
                            NDIS_STATUS_SUCCESS);
    // ------------------------------------------------------------------------------------
    
    return NDIS_STATUS_SUCCESS;
}



VOID
MPReceiveBufferSpaceWorkItem(
    IN PVOID                        WorkItemContext,
    IN NDIS_HANDLE                  NdisIoWorkItemHandle
    )
{
    PNDIS_OID_REQUEST       pOidRequest1 = (PNDIS_OID_REQUEST) WorkItemContext;      

    //UNREFERENCED_PARAMETER(WorkItemContext);
    
    // ------------------------------------------------------------------------------------
    // SDV - Injected Defect - Rule: DoubleCompleteWorkItem
    // Note: For this OID the driver will schedule a work item.
    //        In this case the request is double completed because NdisMOidRquestcomplete 
    //        is called twice.
    // Note: Remember to declare MPReceiveBufferSpaceWorkItem using the appropriate 
    //       Role Type. In this case NDIS_IO_WORKITEM.
    // ------------------------------------------------------------------------------------
    
    // Do something here
    NdisMOidRequestComplete(NULL, pOidRequest1, NDIS_STATUS_SUCCESS);
    
    // Do something else
    NdisMOidRequestComplete(NULL, pOidRequest1, NDIS_STATUS_SUCCESS);
    
    // ------------------------------------------------------------------------------------
    
    NdisFreeIoWorkItem((NDIS_HANDLE)NdisIoWorkItemHandle);
    // ------------------------------------------------------------------------------------
}

NDIS_STATUS 
MPOidRequest(
    IN  NDIS_HANDLE        MiniportAdapterContext,
    IN  PNDIS_OID_REQUEST  NdisRequest
    )
{
    NDIS_STATUS retVal = NDIS_STATUS_INVALID_OID;
    PMP_ADAPTER     pAdapter = (PMP_ADAPTER) MiniportAdapterContext;
    
    // Note: You should never return NDIS_STATUS_SUCCESS by default from MPOidRequest,
    //        in this case we did it to show how the srule works   
        
    switch(NdisRequest->RequestType)
    {
      case NdisRequestQueryInformation:
        // ------------------------------------------------------------------------------------
        // SDV - Injected Defect - Rule: MandatoryOid
        // Note: Returning INVALID_OID or NOT_SUPPORTED is not valid for a defined set of 
        //        OIDs
        // ------------------------------------------------------------------------------------ 
        if (NdisRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_MAXIMUM_TOTAL_SIZE)
        {
          return NDIS_STATUS_INVALID_OID;
        }
        // ------------------------------------------------------------------------------------
        
        // ------------------------------------------------------------------------------------
        // SDV - Injected Defect - Rule: MandatoryWirelessOid
        // Note: Returning INVALID_OID or NOT_SUPPORTED is not valid for a defined set of 
        //        OIDs
        // ------------------------------------------------------------------------------------ 
        if (NdisRequest->DATA.QUERY_INFORMATION.Oid == OID_DOT11_BEACON_PERIOD)
        {
          return NDIS_STATUS_NOT_SUPPORTED;
        }
        // ------------------------------------------------------------------------------------


        if (NdisRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_STATISTICS)
        {
            // calling the function that will handle the OID
            retVal = NICQueryGenStatistics(
                            MiniportAdapterContext,
                            NdisRequest);
                            
            return retVal;
        }
        
        if (NdisRequest->DATA.QUERY_INFORMATION.Oid == OID_GEN_RECEIVE_BUFFER_SPACE)
        {
            
            NDIS_HANDLE NdisIoWorkItemHandle = NdisAllocateIoWorkItem(pAdapter->AdapterHandle);
         
            // schedule the work item to handle the OID
            // check with Nazan/Mitesh it is OK to pass NdisRequest       
            NdisQueueIoWorkItem(NdisIoWorkItemHandle,
                                    MPReceiveBufferSpaceWorkItem,
                                    NdisRequest);
                                    
            return NDIS_STATUS_PENDING;
        }
          
        // Implement ethe rest of the mandatory OIDs
        // Note: You should never return NDIS_STATUS_SUCCESS by default from MPOidRequest,
        //        in this case we did it to show how the rule works   
        return NDIS_STATUS_SUCCESS;
                
        break;

      case NdisRequestSetInformation:
      case NdisRequestQueryStatistics:
      case NdisRequestOpen:
      case NdisRequestClose:
      case NdisRequestSend:
      case NdisRequestTransferData:
      case NdisRequestReset:
      case NdisRequestMethod:
      
        // Implement ethe rest of the mandatory OIDs
        // Note: You should never return NDIS_STATUS_SUCCESS by default from MPOidRequest,
        //        in this case we did it to show how the rule works   
        return NDIS_STATUS_SUCCESS;
        break;
          
      default:
        break;
    }
    
    return retVal;
}


VOID
MPCancelOidRequest(
    IN  NDIS_HANDLE            MiniportAdapterContext,
    IN  PVOID                  RequestId
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
    
    //MINIPORT_CANCEL_OID_REQUEST
}

NDIS_STATUS 
MpRegisterInterrupt(
    PMP_ADAPTER         pAdapter
    )
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;

    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS  Interrupt;

    //
    // the embeded NDIS interrupt structure is already zero'ed out
    // as part of the adapter structure
    //
    NdisZeroMemory(&Interrupt, sizeof(NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS));
    
    Interrupt.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Interrupt.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Interrupt.Header.Size = sizeof(NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS);

    Interrupt.InterruptHandler = MPIsr;
    Interrupt.InterruptDpcHandler = MPHandleInterrupt;
    Interrupt.DisableInterruptHandler = MpDisableInterrupt;
    Interrupt.EnableInterruptHandler = MpEnableInterrupt;

    Status = NdisMRegisterInterruptEx(pAdapter->AdapterHandle,
                                      pAdapter,
                                      &Interrupt,
                                      &pAdapter->InterruptHandle
                                      );

    return Status;
}

VOID MpEnableInterrupt(
    IN PVOID pAdapter
    )
{
    NDIS_SPIN_LOCK DummySpinLock;
    NdisAllocateSpinLock( &DummySpinLock );

    UNREFERENCED_PARAMETER(pAdapter);
    
    //MINIPORT_ENABLE_INTERRUPT


}

VOID MpDisableInterrupt(
    IN PVOID pAdapter
    )
{
    UNREFERENCED_PARAMETER(pAdapter);
    // MINIPORT_DISABLE_INTERRUPT;
}

BOOLEAN MPSynchronizeInterrupt(
    IN PVOID  SynchronizeContext
    )
{
    UNREFERENCED_PARAMETER(SynchronizeContext);
    //MINIPORT_SYNCHRONIZE_INTERRUPT
    return TRUE;
}

NDIS_STATUS
MpQueueTimer(
    PMP_ADAPTER         pAdapter
    )
{
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;
    LARGE_INTEGER   liDueTime;
    NDIS_TIMER_CHARACTERISTICS                      Timer;               

    //         
    NdisZeroMemory(&Timer, sizeof(NDIS_TIMER_CHARACTERISTICS));
    
    Timer.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
    Timer.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
    Timer.Header.Size = sizeof(NDIS_TIMER_CHARACTERISTICS);

    Timer.AllocationTag = NIC_TAG;
    Timer.TimerFunction = MpDemonstrationTimer;
    Timer.FunctionContext = pAdapter;

    //
    // Minimize init-time
    //
    Status = NdisAllocateTimerObject(
        pAdapter->AdapterHandle,
        &Timer,
        &pAdapter->TimerHandle);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    liDueTime.QuadPart = NIC_LINK_DETECTION_DELAY;
    
    // Set a periodic timer
    NdisSetTimerObject(pAdapter->TimerHandle, liDueTime, 10, NULL);

    return NDIS_STATUS_SUCCESS;
}

VOID 
MpDemonstrationTimer(
    IN  PVOID	    SystemSpecific1,
    IN  PVOID	    FunctionContext,
    IN  PVOID	    SystemSpecific2, 
    IN  PVOID	    SystemSpecific3
    )
{
    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(FunctionContext);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);
    
    //NDIS_TIMER_FUNCTION
}

VOID MPProcessSGList(
    IN PDEVICE_OBJECT  pDO,
    IN PVOID  Reserved,
    IN PSCATTER_GATHER_LIST  pSGL,
    IN PVOID  Context
    )
{

    NDIS_SPIN_LOCK DummySpinLock;
    NdisAllocateSpinLock( &DummySpinLock );

    //MINIPORT_PROCESS_SG_LIST
    
}


VOID
MPHandleInterrupt(
    IN  PVOID  MiniportInterruptContext,
    IN  PVOID  MiniportDpcContext,
    IN  PVOID  NdisReserved1,
    IN  PVOID  NdisReserved2
    )
{
    PMP_ADAPTER  pAdapter = (PMP_ADAPTER)MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportInterruptContext);
    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved1);
    UNREFERENCED_PARAMETER(NdisReserved2);

#ifdef ENABLE_MORE_CODE
    //
    // Re-enable the interrupt
    //
    NdisMSynchronizeWithInterruptEx(
        pAdapter->InterruptHandle,
        0,
        MPSynchronizeInterrupt,
        pAdapter
        );
#endif
}

VOID
  MPAllocateComplete(
    IN NDIS_HANDLE  MiniportAdapterContext,
    IN PVOID  VirtualAddress,
    IN PNDIS_PHYSICAL_ADDRESS  PhysicalAddress,
    IN ULONG  Length,
    IN PVOID  Context
    )
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(PhysicalAddress);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Context);
    
    //MINIPORT_ALLOCATE_SHARED_MEM_COMPLETE
} 
    
NDIS_STATUS 
NICAllocAdapterMemory(
    IN  PMP_ADAPTER     pAdapter
    )
{
    NDIS_STATUS     Status;
    NDIS_SG_DMA_DESCRIPTION     DmaDescription;

    NdisZeroMemory(&DmaDescription, sizeof(DmaDescription));
    
    DmaDescription.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    DmaDescription.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    DmaDescription.Header.Size = sizeof(NDIS_SG_DMA_DESCRIPTION);
    DmaDescription.ProcessSGListHandler = MPProcessSGList;
    DmaDescription.SharedMemAllocateCompleteHandler = MPAllocateComplete;
    
    Status = NdisMRegisterScatterGatherDma(
                pAdapter->AdapterHandle,
                &DmaDescription,
                &pAdapter->NdisMiniportDmaHandle);
                
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisWriteErrorLogEntry(
            pAdapter->AdapterHandle,
            NDIS_ERROR_CODE_OUT_OF_RESOURCES,
            1,
            0); 
    }

    return Status;
}

VOID
MPQueuedWorkItem(
    IN PVOID                        WorkItemContext,
    IN NDIS_HANDLE                  NdisIoWorkItemHandle
    )
{
    // NOP   
    
    //NDIS_IO_WORKITEM
    NdisFreeIoWorkItem((NDIS_HANDLE)NdisIoWorkItemHandle);
}