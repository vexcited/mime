/* the video context and device methods that are not backed by anything, generated from the interface lists: they
   return what Direct3D returns for an object that cannot exist here, E_NOTIMPL or nothing. */

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputTargetRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL enable, const RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputBackgroundColor(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL y_cb_cr, const D3D11_VIDEO_COLOR *color) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputColorSpace(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputAlphaFillMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE alpha_fill_mode, UINT stream_idx) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputConstriction(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL enable, SIZE size) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputStereoMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL enable) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputTargetRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL *enabled, RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputBackgroundColor(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL *y_cb_cr, D3D11_VIDEO_COLOR *color) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputColorSpace(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputAlphaFillMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE *alpha_fill_mode, UINT *stream_idx) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputConstriction(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL *enabled, SIZE *size) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputStereoMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL *enabled) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamFrameFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_FRAME_FORMAT format) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamColorSpace(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamOutputRate(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE rate, BOOL repeat, const DXGI_RATIONAL *custom_rate) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamSourceRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamDestRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamAlpha(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, float alpha) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamPalette(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, const UINT *entries) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamPixelAspectRatio(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, const DXGI_RATIONAL *src_aspect_ratio, const DXGI_RATIONAL *dst_aspect_ratio) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamLumaKey(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, float lower, float upper) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamStereoFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT format, BOOL left_view_frame0, BOOL base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE flip_mode, int mono_offset) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamAutoProcessingMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamFilter(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL enable, int level) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamFrameFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_FRAME_FORMAT *format) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamColorSpace(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamOutputRate(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE *rate, BOOL *repeat, DXGI_RATIONAL *custom_rate) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamSourceRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamDestRect(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, RECT *rect) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamAlpha(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, float *alpha) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamPalette(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, UINT entry_count, UINT *entries) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamPixelAspectRatio(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, DXGI_RATIONAL *src_aspect_ratio, DXGI_RATIONAL *dst_aspect_ratio) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamLumaKey(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, float *lower, float *upper) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamStereoFormat(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT *format, BOOL *left_view_frame0, BOOL *base_view_frame0, D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE *flip_mode, int *mono_offset) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamAutoProcessingMode(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enabled) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamFilter(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, D3D11_VIDEO_PROCESSOR_FILTER filter, BOOL *enabled, int *level) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamExtension(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, const GUID *guid, UINT data_size, void *data)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorBlt(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, ID3D11VideoProcessorOutputView *view, UINT frame_idx, UINT stream_count, const D3D11_VIDEO_PROCESSOR_STREAM *streams)
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

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamRotation(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL enable, D3D11_VIDEO_PROCESSOR_ROTATION rotation) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamRotation(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_idx, BOOL *enable, D3D11_VIDEO_PROCESSOR_ROTATION *rotation) {}

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

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputColorSpace1(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, DXGI_COLOR_SPACE_TYPE colour_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetOutputShaderUsage(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL shader_usage) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputColorSpace1(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, DXGI_COLOR_SPACE_TYPE *colour_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetOutputShaderUsage(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, BOOL *shader_usage) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamColorSpace1(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_COLOR_SPACE_TYPE colour_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorSetStreamMirror(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_index, BOOL enable, BOOL flip_horizontal, BOOL flip_vertical) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamColorSpace1(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_index, DXGI_COLOR_SPACE_TYPE *colour_space) {}

static void STDMETHODCALLTYPE vc_stub_VideoProcessorGetStreamMirror(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT stream_index, BOOL *enable, BOOL *flip_horizontal, BOOL *flip_vertical) {}

static HRESULT STDMETHODCALLTYPE vc_stub_VideoProcessorGetBehaviorHints(ID3D11VideoContext1 *This, ID3D11VideoProcessor *processor, UINT output_width, UINT output_height, DXGI_FORMAT output_format, UINT stream_count, const D3D11_VIDEO_PROCESSOR_STREAM_BEHAVIOR_HINT *streams, UINT *behaviour_hints)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CreateVideoProcessor(ID3D11VideoDevice1 *This, ID3D11VideoProcessorEnumerator *pEnum, UINT RateConversionIndex, ID3D11VideoProcessor **ppVideoProcessor)
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

static HRESULT STDMETHODCALLTYPE vd_stub_CreateVideoProcessorInputView(ID3D11VideoDevice1 *This, ID3D11Resource *pResource, ID3D11VideoProcessorEnumerator *pEnum, const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC *pDesc, ID3D11VideoProcessorInputView **ppVPIView)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CreateVideoProcessorOutputView(ID3D11VideoDevice1 *This, ID3D11Resource *pResource, ID3D11VideoProcessorEnumerator *pEnum, const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC *pDesc, ID3D11VideoProcessorOutputView **ppVPOView)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE vd_stub_CreateVideoProcessorEnumerator(ID3D11VideoDevice1 *This, const D3D11_VIDEO_PROCESSOR_CONTENT_DESC *pDesc, ID3D11VideoProcessorEnumerator **ppEnum)
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
