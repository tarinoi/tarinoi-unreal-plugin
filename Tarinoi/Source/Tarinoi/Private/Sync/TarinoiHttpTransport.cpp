// Copyright (c) 2026 Tarinoi Works Oy. MIT licensed; see LICENSE.md.

#include "Sync/TarinoiHttpTransport.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
	class FEngineHttpTransport : public ITarinoiHttpTransport
	{
	public:
		virtual void Get(const FString& Url, const TMap<FString, FString>& Headers,
			TFunction<void(const FTarinoiHttpResponse&)> OnComplete) override
		{
			Request = FHttpModule::Get().CreateRequest();
			Request->SetVerb(TEXT("GET"));
			Request->SetURL(Url);
			Request->SetTimeout(300.0f);
			for (const TPair<FString, FString>& Header : Headers)
			{
				Request->SetHeader(Header.Key, Header.Value);
			}

			// The HTTP module completes on the game thread by default, which is where the
			// importer needs to be to touch the database.
			Request->OnProcessRequestComplete().BindLambda(
				[OnComplete = MoveTemp(OnComplete)](FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bConnected)
				{
					FTarinoiHttpResponse Response;
					if (bConnected && HttpResponse.IsValid())
					{
						Response.Status = HttpResponse->GetResponseCode();
						Response.Body = HttpResponse->GetContentAsString();
					}
					else
					{
						Response.TransportError = TEXT("the server could not be reached");
					}
					OnComplete(Response);
				});

			Request->ProcessRequest();
		}

		virtual void Cancel() override
		{
			if (Request.IsValid())
			{
				Request->OnProcessRequestComplete().Unbind();
				Request->CancelRequest();
				Request.Reset();
			}
		}

	private:
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request;
	};
}

TSharedRef<ITarinoiHttpTransport> ITarinoiHttpTransport::CreateDefault()
{
	return MakeShared<FEngineHttpTransport>();
}
