// --- Includes ---
#include "SkullbonezTextureCollectionDX.h"
#include "SkullbonezWindowDX.h"
#include "stb_image.h"


// --- Usings ---
using namespace SkullbonezCore::Textures;


TextureCollection::TextureCollection()
{
    m_nextAvailableTextureIndex = 0;
    m_textureCounter = 0;

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        m_srvArray[count] = nullptr;
        m_textureData[count] = nullptr;
        m_textureHashes[count] = 0;
    }
}


TextureCollection* TextureCollection::Instance()
{
    if ( !TextureCollection::pInstance )
    {
        static TextureCollection instance;
        TextureCollection::pInstance = &instance;
    }
    return TextureCollection::pInstance;
}


void TextureCollection::Destroy()
{
    if ( TextureCollection::pInstance )
    {
        TextureCollection::pInstance->DeleteAllTextures();
        TextureCollection::pInstance = nullptr;
    }
}


void TextureCollection::UpdateCounters()
{
    m_textureCounter = 0;
    bool isNextAvailIndexSet = false;

    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( !m_textureData[count] )
        {
            if ( !isNextAvailIndexSet )
            {
                m_nextAvailableTextureIndex = count;
                isNextAvailIndexSet = true;
            }
        }
        else
        {
            ++m_textureCounter;
        }
    }
}


int TextureCollection::FindIndex( uint32_t hash )
{
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureHashes[count] == hash )
        {
            return count;
        }
    }

    throw std::runtime_error( "Texture does not exist.  (TextureCollection::FindIndex)" );
}


void TextureCollection::DeleteAllTextures()
{
    for ( int count = 0; count < TOTAL_TEXTURE_COUNT; ++count )
    {
        if ( m_textureData[count] )
        {
            m_srvArray[count] = nullptr;
            m_textureData[count] = nullptr;
            m_textureHashes[count] = 0;
        }
    }

    UpdateCounters();
}


void TextureCollection::DeleteTexture( uint32_t hash )
{
    int index = FindIndex( hash );

    m_srvArray[index] = nullptr;
    m_textureData[index] = nullptr;
    m_textureHashes[index] = 0;

    UpdateCounters();
}


int TextureCollection::NumFreeTextureSpaces()
{
    return TOTAL_TEXTURE_COUNT - m_textureCounter;
}


void TextureCollection::SelectTexture( uint32_t hash )
{
    int index = FindIndex( hash );
    ID3D11DeviceContext* pDeviceContext = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDeviceContext();
    pDeviceContext->PSSetShaderResources( 0, 1, m_srvArray[index].GetAddressOf() );
}


void TextureCollection::CreateJpegTexture( const char* cFileName,
                                           uint32_t hash )
{
    if ( m_textureCounter == TOTAL_TEXTURE_COUNT )
    {
        throw std::runtime_error( "Texture array full!  (TextureCollection::CreateJpegTexture)" );
    }

    m_textureHashes[m_nextAvailableTextureIndex] = hash;

    // Load image via stb_image
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load( cFileName, &width, &height, &channels, 3 );

    if ( !data )
    {
        throw std::runtime_error( "Image load failed!  (TextureCollection::CreateJpegTexture)" );
    }

    ID3D11Device* pDevice = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDevice();

    // Create texture description
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 0;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    textureDesc.CPUAccessFlags = 0;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    // Create initial subresource data
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = width * 3;
    initData.SysMemSlicePitch = 0;

    // Create texture
    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = pDevice->CreateTexture2D( &textureDesc, &initData, &pTexture );

    if ( FAILED( hr ) )
    {
        stbi_image_free( data );
        throw std::runtime_error( "CreateTexture2D failed!  (TextureCollection::CreateJpegTexture)" );
    }

    m_textureData[m_nextAvailableTextureIndex].Attach( pTexture );

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = pDevice->CreateShaderResourceView( pTexture, &srvDesc, &pSRV );

    if ( FAILED( hr ) )
    {
        stbi_image_free( data );
        throw std::runtime_error( "CreateShaderResourceView failed!  (TextureCollection::CreateJpegTexture)" );
    }

    m_srvArray[m_nextAvailableTextureIndex].Attach( pSRV );

    // Generate mipmaps
    ID3D11DeviceContext* pDeviceContext = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDeviceContext();
    pDeviceContext->GenerateMips( pSRV );

    stbi_image_free( data );

    UpdateCounters();
}
