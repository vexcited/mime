/* the video context and device methods that are not backed by anything, generated from the interface lists: they
   return what Direct3D returns for an object that cannot exist here, E_NOTIMPL or nothing. */

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamPalette(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, const UINT *entries) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamStereoFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT format, BOOL left_view_frame0, BOOL base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE flip_mode, int mono_offset) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamPalette(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, UINT *entries) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamStereoFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT *format, BOOL *left_view_frame0, BOOL *base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE *flip_mode, int *mono_offset) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_NegotiateCryptoSessionKeyExchange(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE vc_stub_EncryptionBlt(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, ID3D11Texture2D *src_surface, ID3D11Texture2D *dst_surface, UINT iv_size, void *iv) {}

static void STDMETHODCALLTYPE vc_stub_DecryptionBlt(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, ID3D11Texture2D *src_surface, ID3D11Texture2D *dst_surface, D3D11_ENCRYPTED_BLOCK_INFO *block_info, UINT key_size, const void *key, UINT iv_size, void *iv) {}

static void STDMETHODCALLTYPE vc_stub_StartSessionKeyRefresh(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, UINT random_number_size, void *random_number) {}

static void STDMETHODCALLTYPE vc_stub_FinishSessionKeyRefresh(ID3D11VideoContext1 *This, ID3D11CryptoSession *session) {}

static HRESULT STDMETHODCALLTYPE vc_stub_GetEncryptionBltKey(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, UINT key_size, void *key)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_NegotiateAuthenticatedChannelKeyExchange(ID3D11VideoContext1 *This, ID3D11AuthenticatedChannel *channel, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_QueryAuthenticatedChannel(ID3D11VideoContext1 *This, ID3D11AuthenticatedChannel *channel, UINT input_size, const void *input, UINT output_size, void *output)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_ConfigureAuthenticatedChannel(ID3D11VideoContext1 *This, ID3D11AuthenticatedChannel *channel, UINT input_size, const void *input, D3D11_AUTHENTICATED_CONFIGURE_OUTPUT *output)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_SubmitDecoderBuffers1(ID3D11VideoContext1 *This, ID3D11VideoDecoder *decoder, UINT buffer_count, const D3D11_VIDEO_DECODER_BUFFER_DESC1 *buffer_desc)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_GetDataForNewHardwareKey(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, UINT input_size, const void *input_data, UINT64 *output_data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_CheckCryptoSessionStatus(ID3D11VideoContext1 *This, ID3D11CryptoSession *session, D3D11_CRYPTO_SESSION_STATUS *status)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_DecoderEnableDownsampling(ID3D11VideoContext1 *This, ID3D11VideoDecoder *decoder, DXGI_COLOR_SPACE_TYPE colour_space, const D3D11_VIDEO_SAMPLE_DESC *output_desc, UINT reference_frame_count)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_DecoderUpdateDownsampling(ID3D11VideoContext1 *This, ID3D11VideoDecoder *decoder, const D3D11_VIDEO_SAMPLE_DESC *output_desc)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetBehaviorHints(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT output_width, UINT output_height, DXGI_FORMAT output_format, UINT stream_count, const D3D11_VIDEO_PROCESSOR_STREAM_BEHAVIOR_HINT *streams, UINT *behaviour_hints)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CreateAuthenticatedChannel(ID3D11VideoDevice1 *This, D3D11_AUTHENTICATED_CHANNEL_TYPE ChannelType, ID3D11AuthenticatedChannel **ppAuthenticatedChannel)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CreateCryptoSession(ID3D11VideoDevice1 *This, const GUID *pCryptoType, const GUID *pDecoderProfile, const GUID *pKeyExchangeType, ID3D11CryptoSession **ppCryptoSession)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_GetContentProtectionCaps(ID3D11VideoDevice1 *This, const GUID *pCryptoType, const GUID *pDecoderProfile, D3D11_VIDEO_CONTENT_PROTECTION_CAPS *pCaps)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CheckCryptoKeyExchange(ID3D11VideoDevice1 *This, const GUID *pCryptoType, const GUID *pDecoderProfile, UINT Index, GUID *pKeyExchangeType)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_GetCryptoSessionPrivateDataSize(ID3D11VideoDevice1 *This, const GUID *crypto_type, const GUID *decoder_profile, const GUID *key_exchange_type, UINT *input_size, UINT *output_size)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_RecommendVideoDecoderDownsampleParameters(ID3D11VideoDevice1 *This, const D3D11_VIDEO_DECODER_DESC *input_desc, DXGI_COLOR_SPACE_TYPE input_colour_space, const D3D11_VIDEO_DECODER_CONFIG *input_config, const DXGI_RATIONAL *framerate, D3D11_VIDEO_SAMPLE_DESC *recommended_output_desc)
{
    return E_NOTIMPL;
}
