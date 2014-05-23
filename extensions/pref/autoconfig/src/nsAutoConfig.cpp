/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_LOGGING
// sorry, this has to be before the pre-compiled header
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif
#include "nsAutoConfig.h"
#include "nsIURI.h"
#include "nsIHttpChannel.h"
#include "nsIFileStreams.h"
#include "nsThreadUtils.h"
#include "nsAppDirectoryServiceDefs.h"
#include "prmem.h"
#include "nsIObserverService.h"
#include "nsLiteralString.h"
#include "nsIPromptService.h"
#include "nsIServiceManager.h"
#include "nsIStringBundle.h"
#include "nsCRT.h"
#include "nspr.h"
#include <algorithm>

PRLogModuleInfo *MCD;

extern nsresult EvaluateAdminConfigScript(const char *js_buffer, size_t length,
                                          const char *filename, 
                                          bool bGlobalContext, 
                                          bool bCallbacks, 
                                          bool skipFirstLine);

// nsISupports Implementation

NS_IMPL_ISUPPORTS(nsAutoConfig, nsIAutoConfig, nsITimerCallback, nsIStreamListener, nsIObserver, nsIRequestObserver, nsISupportsWeakReference)

nsAutoConfig::nsAutoConfig()
{
}

nsresult nsAutoConfig::Init()
{
    // member initializers and constructor code

    nsresult rv;
    mLoaded = false;
    
    // Registering the object as an observer to the profile-after-change topic
    nsCOMPtr<nsIObserverService> observerService =
        do_GetService("@mozilla.org/observer-service;1", &rv);
    if (NS_FAILED(rv)) 
        return rv;

    rv = observerService->AddObserver(this,"profile-after-change", true);
    
    return rv;
}

nsAutoConfig::~nsAutoConfig()
{
}

// attribute string configURL
NS_IMETHODIMP nsAutoConfig::GetConfigURL(char **aConfigURL)
{
    if (!aConfigURL) 
        return NS_ERROR_NULL_POINTER;

    if (mConfigURL.IsEmpty()) {
        *aConfigURL = nullptr;
        return NS_OK;
    }
    
    *aConfigURL = ToNewCString(mConfigURL);
    if (!*aConfigURL)
        return NS_ERROR_OUT_OF_MEMORY;
    return NS_OK;
}
NS_IMETHODIMP nsAutoConfig::SetConfigURL(const char *aConfigURL)
{
    if (!aConfigURL)
        return NS_ERROR_NULL_POINTER;
    mConfigURL.Assign(aConfigURL);
    return NS_OK;
}

NS_IMETHODIMP
nsAutoConfig::OnStartRequest(nsIRequest *request, nsISupports *context)
{
    return NS_OK;
}


NS_IMETHODIMP
nsAutoConfig::OnDataAvailable(nsIRequest *request, 
                              nsISupports *context,
                              nsIInputStream *aIStream, 
                              uint64_t aSourceOffset,
                              uint32_t aLength)
{    
    uint32_t amt, size;
    nsresult rv;
    char buf[1024];
    
    while (aLength) {
        size = std::min<size_t>(aLength, sizeof(buf));
        rv = aIStream->Read(buf, size, &amt);
        if (NS_FAILED(rv))
            return rv;
        mBuf.Append(buf, amt);
        aLength -= amt;
    }
    return NS_OK;
}


NS_IMETHODIMP
nsAutoConfig::OnStopRequest(nsIRequest *request, nsISupports *context,
                            nsresult aStatus)
{
    nsresult rv;

    // If the request is failed, go read the failover.jsc file
    if (NS_FAILED(aStatus)) {
        PR_LOG(MCD, PR_LOG_DEBUG, ("mcd request failed with status %x\n", aStatus));
        return readOfflineFile();
    }

    // Checking for the http response, if failure go read the failover file.
    nsCOMPtr<nsIHttpChannel> pHTTPCon(do_QueryInterface(request));
    if (pHTTPCon) {
        uint32_t httpStatus;
        pHTTPCon->GetResponseStatus(&httpStatus);
        if (httpStatus != 200) 
        {
            PR_LOG(MCD, PR_LOG_DEBUG, ("mcd http request failed with status %x\n", httpStatus));
            return readOfflineFile();
        }
    }
    
    // Send the autoconfig.jsc to javascript engine.
    
    rv = EvaluateAdminConfigScript(mBuf.get(), mBuf.Length(),
                              nullptr, false,true, false);
    if (NS_SUCCEEDED(rv)) {

        // Write the autoconfig.jsc to failover.jsc (cached copy) 
        rv = writeFailoverFile(); 

        if (NS_FAILED(rv)) 
            NS_WARNING("Error writing failover.jsc file");

        // Releasing the lock to allow the main thread to start execution
        mLoaded = true;  

        return NS_OK;
    }
    // there is an error in parsing of the autoconfig file.
    NS_WARNING("Error reading autoconfig.jsc from the network, reading the offline version");
    return readOfflineFile();
}

// Notify method as a TimerCallBack function 
NS_IMETHODIMP nsAutoConfig::Notify(nsITimer *timer) 
{
    downloadAutoConfig();
    return NS_OK;
}

/* Observe() is called twice: once at the instantiation time and other 
   after the profile is set. It doesn't do anything but return NS_OK during the
   creation time. Second time it calls  downloadAutoConfig().
*/

NS_IMETHODIMP nsAutoConfig::Observe(nsISupports *aSubject, 
                                    const char *aTopic, 
                                    const char16_t *someData)
{
    nsresult rv = NS_OK;
    if (!nsCRT::strcmp(aTopic, "profile-after-change")) {

        // We will be calling downloadAutoConfig even if there is no profile 
        // name. Nothing will be passed as a parameter to the URL and the
        // default case will be picked up by the script.
        
        rv = downloadAutoConfig();

    }  
   
    return rv;
}

nsresult nsAutoConfig::downloadAutoConfig()
{
    nsresult rv;
    nsAutoCString emailAddr;
    nsXPIDLCString urlName;
    static bool firstTime = true;
    
    if (mConfigURL.IsEmpty()) {
        PR_LOG(MCD, PR_LOG_DEBUG, ("global config url is empty - did you set autoadmin.global_config_url?\n"));
        NS_WARNING("AutoConfig called without global_config_url");
        return NS_OK;
    }
    
    // If there is an email address appended as an argument to the ConfigURL
    // in the previous read, we need to remove it when timer kicks in and 
    // downloads the autoconfig file again. 
    // If necessary, the email address will be added again as an argument.
    int32_t index = mConfigURL.RFindChar((char16_t)'?');
    if (index != -1)
        mConfigURL.Truncate(index);

    // Clean up the previous read, the new read is going to use the same buffer
    if (!mBuf.IsEmpty())
        mBuf.Truncate(0);

    // Get the preferences branch and save it to the member variable
    if (!mPrefBranch) {
        nsCOMPtr<nsIPrefService> prefs =
            do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
        if (NS_FAILED(rv)) 
            return rv;
    
        rv = prefs->GetBranch(nullptr,getter_AddRefs(mPrefBranch));
        if (NS_FAILED(rv))
            return rv;
    }
    
    // Check to see if the network is online/offline 
    nsCOMPtr<nsIIOService> ios = do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv)) 
        return rv;
    
    bool offline;
    rv = ios->GetOffline(&offline);
    if (NS_FAILED(rv)) 
        return rv;
    
    if (offline) {
        bool offlineFailover;
        rv = mPrefBranch->GetBoolPref("autoadmin.offline_failover", 
                                      &offlineFailover);
        // Read the failover.jsc if the network is offline and the pref says so
        if (NS_SUCCEEDED(rv) && offlineFailover)
            return readOfflineFile();
    }

    /* Append user's identity at the end of the URL if the pref says so.
       First we are checking for the user's email address but if it is not
       available in the case where the client is used without messenger, user's
       profile name will be used as an unique identifier
    */
    bool appendMail;
    rv = mPrefBranch->GetBoolPref("autoadmin.append_emailaddr", &appendMail);
    if (NS_SUCCEEDED(rv) && appendMail) {
        rv = getEmailAddr(emailAddr);
        if (NS_SUCCEEDED(rv) && emailAddr.get()) {
            /* Adding the unique identifier at the end of autoconfig URL. 
               In this case the autoconfig URL is a script and 
               emailAddr as passed as an argument 
            */
            mConfigURL.Append('?');
            mConfigURL.Append(emailAddr); 
        }
    }
    
    // create a new url 
    nsCOMPtr<nsIURI> url;
    nsCOMPtr<nsIChannel> channel;
    
    rv = NS_NewURI(getter_AddRefs(url), mConfigURL.get(), nullptr, nullptr);
    if (NS_FAILED(rv))
    {
        PR_LOG(MCD, PR_LOG_DEBUG, ("failed to create URL - is autoadmin.global_config_url valid? - %s\n", mConfigURL.get()));
        return rv;
    }

    PR_LOG(MCD, PR_LOG_DEBUG, ("running MCD url %s\n", mConfigURL.get()));
    // open a channel for the url
    rv = NS_NewChannel(getter_AddRefs(channel),url, nullptr, nullptr, nullptr, nsIRequest::INHIBIT_PERSISTENT_CACHING | nsIRequest::LOAD_BYPASS_CACHE);
    if (NS_FAILED(rv)) 
        return rv;

    rv = channel->AsyncOpen(this, nullptr); 
    if (NS_FAILED(rv)) {
        readOfflineFile();
        return rv;
    }
    
    // Set a repeating timer if the pref is set.
    // This is to be done only once.
    // Also We are having the event queue processing only for the startup
    // It is not needed with the repeating timer.
    if (firstTime) {
        firstTime = false;
    
        // Getting the current thread. If we start an AsyncOpen, the thread
        // needs to wait before the reading of autoconfig is done

        nsCOMPtr<nsIThread> thread = do_GetCurrentThread();
        NS_ENSURE_STATE(thread);
    
        /* process events until we're finished. AutoConfig.jsc reading needs
           to be finished before the browser starts loading up
           We are waiting for the mLoaded which will be set through 
           onStopRequest or readOfflineFile methods
           There is a possibility of deadlock so we need to make sure
           that mLoaded will be set to true in any case (success/failure)
        */
        
        while (!mLoaded)
            NS_ENSURE_STATE(NS_ProcessNextEvent(thread));
        
        int32_t minutes;
        rv = mPrefBranch->GetIntPref("autoadmin.refresh_interval", 
                                     &minutes);
        if (NS_SUCCEEDED(rv) && minutes > 0) {
            // Create a new timer and pass this nsAutoConfig 
            // object as a timer callback. 
            mTimer = do_CreateInstance("@mozilla.org/timer;1",&rv);
            if (NS_FAILED(rv)) 
                return rv;
            rv = mTimer->InitWithCallback(this, minutes * 60 * 1000, 
                             nsITimer::TYPE_REPEATING_SLACK);
            if (NS_FAILED(rv)) 
                return rv;
        }
    } //first_time
    
    return NS_OK;
} // nsPref::downloadAutoConfig()



nsresult nsAutoConfig::readOfflineFile()
{
    nsresult rv;
    
    /* Releasing the lock to allow main thread to start 
       execution. At this point we do not need to stall 
       the thread since all network activities are done.
    */
    mLoaded = true; 

    bool failCache;
    rv = mPrefBranch->GetBoolPref("autoadmin.failover_to_cached", &failCache);
    if (NS_SUCCEEDED(rv) && !failCache) {
        // disable network connections and return.
        
        nsCOMPtr<nsIIOService> ios =
            do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
        if (NS_FAILED(rv)) 
            return rv;
        
        bool offline;
        rv = ios->GetOffline(&offline);
        if (NS_FAILED(rv)) 
            return rv;

        if (!offline) {
            rv = ios->SetOffline(true);
            if (NS_FAILED(rv)) 
                return rv;
        }
        
        // lock the "network.online" prference so user cannot toggle back to
        // online mode.
        rv = mPrefBranch->SetBoolPref("network.online", false);
        if (NS_FAILED(rv)) 
            return rv;

        mPrefBranch->LockPref("network.online");
        return NS_OK;
    }
    
    /* faiover_to_cached is set to true so 
       Open the file and read the content.
       execute the javascript file
    */
    
    nsCOMPtr<nsIFile> failoverFile; 
    rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                getter_AddRefs(failoverFile));
    if (NS_FAILED(rv)) 
        return rv;
    
    failoverFile->AppendNative(NS_LITERAL_CSTRING("failover.jsc"));
    rv = evaluateLocalFile(failoverFile);
    if (NS_FAILED(rv)) 
        NS_WARNING("Couldn't open failover.jsc, going back to default prefs");
    return NS_OK;
}

nsresult nsAutoConfig::evaluateLocalFile(nsIFile *file)
{
    nsresult rv;
    nsCOMPtr<nsIInputStream> inStr;
    
    rv = NS_NewLocalFileInputStream(getter_AddRefs(inStr), file);
    if (NS_FAILED(rv)) 
        return rv;
        
    int64_t fileSize;
    file->GetFileSize(&fileSize);
    uint32_t fs = fileSize; // Converting 64 bit structure to unsigned int
    char *buf = (char *)PR_Malloc(fs * sizeof(char));
    if (!buf) 
        return NS_ERROR_OUT_OF_MEMORY;

    uint32_t amt = 0;
    rv = inStr->Read(buf, fs, &amt);
    if (NS_SUCCEEDED(rv)) {
      EvaluateAdminConfigScript(buf, fs, nullptr, false, 
                                true, false);
    }
    inStr->Close();
    PR_Free(buf);
    return rv;
}

nsresult nsAutoConfig::writeFailoverFile()
{
    nsresult rv;
    nsCOMPtr<nsIFile> failoverFile; 
    nsCOMPtr<nsIOutputStream> outStr;
    uint32_t amt;
    
    rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                getter_AddRefs(failoverFile));
    if (NS_FAILED(rv)) 
        return rv;
    
    failoverFile->AppendNative(NS_LITERAL_CSTRING("failover.jsc"));
    
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(outStr), failoverFile);
    if (NS_FAILED(rv)) 
        return rv;
    rv = outStr->Write(mBuf.get(),mBuf.Length(),&amt);
    outStr->Close();
    return rv;
}

nsresult nsAutoConfig::getEmailAddr(nsACString & emailAddr)
{
    
    nsresult rv;
    nsXPIDLCString prefValue;
    
    /* Getting an email address through set of three preferences:
       First getting a default account with 
       "mail.accountmanager.defaultaccount"
       second getting an associated id with the default account
       Third getting an email address with id
    */
    
    rv = mPrefBranch->GetCharPref("mail.accountmanager.defaultaccount", 
                                  getter_Copies(prefValue));
    if (NS_SUCCEEDED(rv) && !prefValue.IsEmpty()) {
        emailAddr = NS_LITERAL_CSTRING("mail.account.") +
            prefValue + NS_LITERAL_CSTRING(".identities");
        rv = mPrefBranch->GetCharPref(PromiseFlatCString(emailAddr).get(),
                                      getter_Copies(prefValue));
        if (NS_FAILED(rv) || prefValue.IsEmpty())
            return PromptForEMailAddress(emailAddr);
        int32_t commandIndex = prefValue.FindChar(',');
        if (commandIndex != kNotFound)
          prefValue.Truncate(commandIndex);
        emailAddr = NS_LITERAL_CSTRING("mail.identity.") +
            prefValue + NS_LITERAL_CSTRING(".useremail");
        rv = mPrefBranch->GetCharPref(PromiseFlatCString(emailAddr).get(),
                                      getter_Copies(prefValue));
        if (NS_FAILED(rv)  || prefValue.IsEmpty())
            return PromptForEMailAddress(emailAddr);
        emailAddr = prefValue;
    }
    else {
        // look for 4.x pref in case we just migrated.
        rv = mPrefBranch->GetCharPref("mail.identity.useremail", 
                                  getter_Copies(prefValue));
        if (NS_SUCCEEDED(rv) && !prefValue.IsEmpty())
            emailAddr = prefValue;
        else
            PromptForEMailAddress(emailAddr);
    }
    
    return NS_OK;
}
        
nsresult nsAutoConfig::PromptForEMailAddress(nsACString &emailAddress)
{
    nsresult rv;
    nsCOMPtr<nsIPromptService> promptService = do_GetService("@mozilla.org/embedcomp/prompt-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIStringBundleService> bundleService = do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIStringBundle> bundle;
    rv = bundleService->CreateBundle("chrome://autoconfig/locale/autoconfig.properties",
                                getter_AddRefs(bundle));
    NS_ENSURE_SUCCESS(rv, rv);

    nsXPIDLString title;
    rv = bundle->GetStringFromName(MOZ_UTF16("emailPromptTitle"), getter_Copies(title));
    NS_ENSURE_SUCCESS(rv, rv);

    nsXPIDLString err;
    rv = bundle->GetStringFromName(MOZ_UTF16("emailPromptMsg"), getter_Copies(err));
    NS_ENSURE_SUCCESS(rv, rv);
    bool check = false;
    nsXPIDLString emailResult;
    bool success;
    rv = promptService->Prompt(nullptr, title.get(), err.get(), getter_Copies(emailResult), nullptr, &check, &success);
    if (!success)
      return NS_ERROR_FAILURE;
    NS_ENSURE_SUCCESS(rv, rv);
    LossyCopyUTF16toASCII(emailResult, emailAddress);
    return NS_OK;
}
