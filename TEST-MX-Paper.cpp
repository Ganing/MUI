/****************************************************************************
 * 标题: MXPaper - 壁纸软件
 * 功能: Logo 纹理立方体 + 地面 + UI 面板 + 几何体创建 + Gizmo 选择/平移/缩放
 * 参考: TEST-Filament-OUI.cpp
 ****************************************************************************/

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <chrono>
#include <algorithm>
#include <vector>
#include <string>
#include <memory>
#include <functional>

#include <windows.h>
#include <mmsystem.h>

#include "ocore.h"
#include "oapp.h"
#include "oui.h"

#include <filament/Engine.h>
#include <filament/Camera.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/Skybox.h>
#include <filament/LightManager.h>
#include <filament/Viewport.h>
#include <filament/Color.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <utils/EntityManager.h>
#include <backend/platforms/PlatformWGL.h>
#include <filamat/MaterialBuilder.h>
#include <glad/glad.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "winmm.lib")

#define FILAMENT_LIB_PATH "e:/MX/3rd/lib/filament/mt/"
#pragma comment(lib, FILAMENT_LIB_PATH "filament.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "backend.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluegl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filabridge.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filaflat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "filamat.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "utils.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "ibl.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "shaders.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "stb.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "zstd.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "bluevk.lib")
#pragma comment(lib, FILAMENT_LIB_PATH "smol-v.lib")
#pragma comment(lib, "e:/MX/3rd/lib/libthorvg_mt.lib")

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

using namespace filament;
using namespace filament::math;
using utils::Entity;
using utils::EntityManager;

enum class AppMode { Edit, Play };
enum GizmoOp { GNone, GTranslate, GRotate, GScale };
struct SceneObj { Entity entity; float3 pos; float3 scl; MaterialInstance* mi; MaterialInstance* selMi; };

/*==================== WGL Platform ====================*/
class NoSwapWGLPlatform : public filament::backend::PlatformWGL {
public:
    void* filamentHGLRC = nullptr;
    filament::backend::Driver* createDriver(void* shared, const Platform::DriverConfig& cfg) override {
        auto* d = PlatformWGL::createDriver(nullptr, cfg);
        if (d) filamentHGLRC = wglGetCurrentContext();
        return d;
    }
    void commit(filament::backend::Platform::SwapChain* sc) noexcept override {}
};

/*==================== AppState ====================*/
struct AppState {
    OX::Application app;
    OX::UIManager ui;
    Engine* engine=nullptr; SwapChain* swapChain=nullptr; Renderer* renderer=nullptr;
    Scene* scene=nullptr; View* view=nullptr; Camera* camera=nullptr; Entity cameraEntity;
    VertexBuffer* vb=nullptr; IndexBuffer* ib=nullptr;
    Material* mat=nullptr; MaterialInstance* mi=nullptr; Texture* logoTexture=nullptr;
    Entity cubeEntity; Entity groundEntity; Skybox* skybox=nullptr;
    float cameraDist=25; float cubeAngle=0; bool autoRotate=true;
    float fps=0; std::chrono::steady_clock::time_point lastFrameTime;
    AppMode mode=AppMode::Edit;
    OX::UILabel* fpsLabel=nullptr; OX::UIButton* rotBtn=nullptr;
    std::vector<OX::UIElement*> editOnly,playOnly;
    std::vector<OX::UIFrame*> panels;
    float Pw=1280, Ph=720;
    // Gizmo
    std::vector<SceneObj> objs; int selObj=-1; GizmoOp gop=GTranslate;
    bool gDrag=false; float2 gStart{}; int mx=0,my=0;
    Material* selMat=nullptr; bool selInit=false;
};

/*==================== Geometry helpers ====================*/
static SceneObj mkBox(AppState& st, float3 pos, float3 scl, float4 color) {
    if (!st.selInit) {
        filamat::MaterialBuilder b;
        b.name("MXSel").material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(1,0.8,0,1);}")
         .shading(Shading::UNLIT).targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
         .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg = b.build(st.engine->getJobSystem());
        if (pkg.isValid()) st.selMat = Material::Builder().package(pkg.getData(), pkg.getSize()).build(*st.engine);
        st.selInit = true;
    }
    filamat::MaterialBuilder bm;
    bm.name("MXObj").material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(getColor().rgb,1.0);}")
      .shading(Shading::UNLIT).require(VertexAttribute::COLOR)
      .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL).platform(filamat::MaterialBuilder::Platform::DESKTOP);
    auto pkg = bm.build(st.engine->getJobSystem());
    auto* m = Material::Builder().package(pkg.getData(), pkg.getSize()).build(*st.engine);
    auto* mi = m->createInstance();
    auto* selMi = st.selMat ? st.selMat->createInstance() : mi;
    uint32_t col = ((uint32_t)(color.r*255)|((uint32_t)(color.g*255)<<8)|((uint32_t)(color.b*255)<<16)|0xff000000u);
    float hx=0.5f, hy=0.5f, hz=0.5f;
    struct V{float3 p;uint32_t c;};
    V v[24]={
        {{hx,-hy,-hz},col},{{hx,-hy,hz},col},{{hx,hy,hz},col},{{hx,hy,-hz},col},
        {{-hx,-hy,hz},col},{{-hx,-hy,-hz},col},{{-hx,hy,-hz},col},{{-hx,hy,hz},col},
        {{-hx,hy,-hz},col},{{hx,hy,-hz},col},{{hx,hy,hz},col},{{-hx,hy,hz},col},
        {{-hx,-hy,hz},col},{{hx,-hy,hz},col},{{hx,-hy,-hz},col},{{-hx,-hy,-hz},col},
        {{-hx,-hy,hz},col},{{hx,-hy,hz},col},{{hx,hy,hz},col},{{-hx,hy,hz},col},
        {{hx,-hy,-hz},col},{{-hx,-hy,-hz},col},{{-hx,hy,-hz},col},{{hx,hy,-hz},col},
    };
    uint16_t id[36]={0,2,1,0,3,2,4,6,5,4,7,6,8,10,9,8,11,10,12,14,13,12,15,14,16,17,18,16,18,19,20,21,22,20,22,23};
    auto vb=VertexBuffer::Builder().vertexCount(24).bufferCount(1)
        .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,0,16)
        .attribute(VertexAttribute::COLOR,0,VertexBuffer::AttributeType::UBYTE4,12,16).normalized(VertexAttribute::COLOR)
        .build(*st.engine);
    vb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(v,sizeof(v),nullptr));
    auto ib=IndexBuffer::Builder().indexCount(36).bufferType(IndexBuffer::IndexType::USHORT).build(*st.engine);
    ib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(id,sizeof(id),nullptr));
    Entity e=EntityManager::get().create();
    RenderableManager::Builder(1).boundingBox({{-1,-1,-1},{1,1,1}})
        .material(0,mi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,vb,ib,0,36)
        .culling(true).receiveShadows(false).castShadows(false).build(*st.engine,e);
    st.scene->addEntity(e);
    auto& t=st.engine->getTransformManager(); t.setTransform(t.getInstance(e),mat4f::translation(pos)*mat4f::scaling(scl));
    return {e,pos,scl,mi,selMi};
}

static SceneObj mkPlane(AppState& st, float3 pos, float3 scl, float4 color) {
    if (!st.selInit) {
        filamat::MaterialBuilder b;
        b.name("MXSel").material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(1,0.8,0,1);}")
         .shading(Shading::UNLIT).targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
         .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg=b.build(st.engine->getJobSystem());
        if(pkg.isValid())st.selMat=Material::Builder().package(pkg.getData(),pkg.getSize()).build(*st.engine);
        st.selInit=true;
    }
    filamat::MaterialBuilder bm;
    bm.name("MXObj2").material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(getColor().rgb,1.0);}")
      .shading(Shading::UNLIT).require(VertexAttribute::COLOR)
      .targetApi(filamat::MaterialBuilder::TargetApi::OPENGL).platform(filamat::MaterialBuilder::Platform::DESKTOP);
    auto pkg=bm.build(st.engine->getJobSystem());
    auto*m=Material::Builder().package(pkg.getData(),pkg.getSize()).build(*st.engine);
    auto*mi=m->createInstance(); auto*selMi=st.selMat?st.selMat->createInstance():mi;
    uint32_t col=((uint32_t)(color.r*255)|((uint32_t)(color.g*255)<<8)|((uint32_t)(color.b*255)<<16)|0xff000000u);
    struct V{float3 p;uint32_t c;};
    V v[4]={{{-0.5f,0,-0.5f},col},{{0.5f,0,-0.5f},col},{{0.5f,0,0.5f},col},{{-0.5f,0,0.5f},col}};
    uint16_t id[6]={0,2,1,0,3,2};
    auto vb=VertexBuffer::Builder().vertexCount(4).bufferCount(1)
        .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,0,16)
        .attribute(VertexAttribute::COLOR,0,VertexBuffer::AttributeType::UBYTE4,12,16).normalized(VertexAttribute::COLOR)
        .build(*st.engine);
    vb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(v,sizeof(v),nullptr));
    auto ib=IndexBuffer::Builder().indexCount(6).bufferType(IndexBuffer::IndexType::USHORT).build(*st.engine);
    ib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(id,sizeof(id),nullptr));
    Entity e=EntityManager::get().create();
    RenderableManager::Builder(1).boundingBox({{-1,-0.1f,-1},{1,0.1f,1}})
        .material(0,mi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,vb,ib,0,6)
        .culling(true).receiveShadows(false).castShadows(false).build(*st.engine,e);
    st.scene->addEntity(e);
    auto&t=st.engine->getTransformManager(); t.setTransform(t.getInstance(e),mat4f::translation(pos)*mat4f::scaling(scl));
    return {e,pos,scl,mi,selMi};
}

/*==================== UI helpers ====================*/
static OX::OColor C(uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){return OX::OColor(r,g,b,a);}
static void addPanel(AppState& st,float x,float y,float w,float h,const char*title,
    std::vector<const char*>items,bool forEdit=true){
    auto fr=std::make_unique<OX::UIFrame>();
    fr->rect=OX::ORect(x,y,w,h); fr->setStyle(C(22,22,44,235),C(255,255,255,18),1,6);
    fr->setPadding(8,28,8,8); fr->setVerticalLayout(); fr->setGap(2);
    auto hdr=std::make_unique<OX::UILabel>(title);
    hdr->rect=OX::ORect(8,6,w-16,20); hdr->fontSize=11; hdr->fontName="siyuan"; hdr->textColor=C(255,255,255,200);
    fr->addChild(std::move(hdr));
    for(auto t:items){
        auto btn=std::make_unique<OX::UIButton>(t);
        btn->rect=OX::ORect(0,0,0,24); btn->fontSize=11; btn->fontName="siyuan"; btn->cornerRadius=4; btn->textAlignX=0.08f;
        btn->setColors(C(0,0,0,0),C(255,255,255,20),C(255,255,255,8),C(120,120,160));
        fr->addChild(std::move(btn));
    }
    OX::UIFrame*p=fr.get(); st.ui.addElement(std::move(fr));
    if(forEdit)st.editOnly.push_back(p); st.panels.push_back(p);
}

/*==================== buildCube ====================*/
static Entity buildCube(AppState& st) {
    static bool initDone=false;
    if(!initDone){filamat::MaterialBuilder::init();initDone=true;}
    { filamat::MaterialBuilder b; b.name("TexUnlit")
        .material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=texture(materialParams_albedo,getUV0());}")
        .shading(Shading::UNLIT).parameter("albedo",filamat::MaterialBuilder::SamplerType::SAMPLER_2D)
        .require(VertexAttribute::UV0).targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
        .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg=b.build(st.engine->getJobSystem());
        st.mat=Material::Builder().package(pkg.getData(),pkg.getSize()).build(*st.engine);
    }
    int tw,th,tn; stbi_uc* px=stbi_load("Data/LOGO.jpg",&tw,&th,&tn,4);
    if(px){
        st.logoTexture=Texture::Builder().width((uint32_t)tw).height((uint32_t)th).levels(1)
            .format(Texture::InternalFormat::RGBA8).sampler(Texture::Sampler::SAMPLER_2D).build(*st.engine);
        Texture::PixelBufferDescriptor pbd(px,(size_t)(tw*th*4),Texture::Format::RGBA,Texture::Type::UBYTE,
            [](void*buf,size_t,void*){stbi_image_free(buf);});
        st.logoTexture->setImage(*st.engine,0,std::move(pbd)); st.engine->flushAndWait();
    }
    st.mi=st.mat->createInstance();
    if(st.logoTexture){
        TextureSampler s(TextureSampler::MinFilter::LINEAR,TextureSampler::MagFilter::LINEAR);
        st.mi->setParameter("albedo",st.logoTexture,s);
    }
    constexpr int kS=5*(int)sizeof(float);
    st.vb=VertexBuffer::Builder().vertexCount(24).bufferCount(1)
        .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,0,kS)
        .attribute(VertexAttribute::UV0,0,VertexBuffer::AttributeType::FLOAT2,3*(int)sizeof(float),kS).build(*st.engine);
    float d[]={
        1,-1,-1,0,1,1,-1,1,1,1,1,1,1,1,0,1,1,-1,0,0,
        -1,-1,1,0,1,-1,-1,-1,1,1,-1,1,-1,1,0,-1,1,1,0,0,
        -1,1,-1,0,1,1,1,-1,1,1,1,1,1,1,0,-1,1,1,0,0,
        -1,-1,1,0,1,1,-1,1,1,1,1,-1,-1,1,0,-1,-1,-1,0,0,
        -1,-1,1,0,1,1,-1,1,1,1,1,1,1,1,0,-1,1,1,0,0,
        1,-1,-1,0,1,-1,-1,-1,1,1,-1,1,-1,1,0,1,1,-1,0,0};
    st.vb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(d,sizeof(d),nullptr));
    uint16_t id[36]={0,2,1,0,3,2,4,6,5,4,7,6,8,10,9,8,11,10,12,14,13,12,15,14,16,17,18,16,18,19,20,21,22,20,22,23};
    st.ib=IndexBuffer::Builder().indexCount(36).bufferType(IndexBuffer::IndexType::USHORT).build(*st.engine);
    st.ib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(id,sizeof(id),nullptr));
    Entity e=EntityManager::get().create();
    RenderableManager::Builder(1).boundingBox({{-1,-1,-1},{1,1,1}})
        .material(0,st.mi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,st.vb,st.ib,0,36)
        .culling(true).receiveShadows(false).castShadows(false).build(*st.engine,e);
    return e;
}

/*==================== MAIN ====================*/
int main(int argc,char*argv[]){
    (void)argc;(void)argv; OX::Core::initConsole(); timeBeginPeriod(1);
    AppState st;
    OX::WindowDesc desc; desc.title=L"MXPaper"; desc.width=1280; desc.height=720;
    desc.enableOpenGL=true; desc.stencilBits=0;
    if(!st.app.create(&desc)){OX_LOG("Window failed\n");timeEndPeriod(1);return 1;}
    auto wsi=(BOOL(WINAPI*)(int))wglGetProcAddress("wglSwapIntervalEXT"); if(wsi)wsi(1);
    uint32_t W,H; st.app.getSize(&W,&H);

    // Filament
    auto* plat=new NoSwapWGLPlatform();
    st.engine=Engine::create(Engine::Backend::OPENGL,plat,nullptr);
    st.swapChain=st.engine->createSwapChain((void*)st.app.getHwnd());
    st.renderer=st.engine->createRenderer(); st.renderer->setClearOptions({{0.03f,0.03f,0.08f,1.0f},true,true});
    st.scene=st.engine->createScene(); st.view=st.engine->createView(); st.view->setScene(st.scene);
    st.view->setViewport(filament::Viewport{0,0,W,H}); st.view->setPostProcessingEnabled(false);
    st.cameraEntity=EntityManager::get().create(); st.camera=st.engine->createCamera(st.cameraEntity); st.view->setCamera(st.camera);
    st.skybox=Skybox::Builder().color({0.08f,0.10f,0.20f,1.0f}).build(*st.engine); st.scene->setSkybox(st.skybox);
    st.cubeEntity=buildCube(st); st.scene->addEntity(st.cubeEntity);

    // Ground
    { filamat::MaterialBuilder b; b.name("Gnd")
        .material("void material(inout MaterialInputs m){prepareMaterial(m);m.baseColor=vec4(0.16,0.16,0.18,1.0);}")
        .shading(Shading::UNLIT).targetApi(filamat::MaterialBuilder::TargetApi::OPENGL)
        .platform(filamat::MaterialBuilder::Platform::DESKTOP);
        auto pkg=b.build(st.engine->getJobSystem());
        if(pkg.isValid()){ auto*gm=Material::Builder().package(pkg.getData(),pkg.getSize()).build(*st.engine);
        auto*gmi=gm->createInstance(); float s=20,gv[]={-s,-0.01f,-s,-s,-0.01f,s,s,-0.01f,s,s,-0.01f,-s};
        uint16_t gi[]={0,1,2,0,2,3};
        auto gvb=VertexBuffer::Builder().vertexCount(4).bufferCount(1)
            .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3).build(*st.engine);
        gvb->setBufferAt(*st.engine,0,VertexBuffer::BufferDescriptor(gv,sizeof(gv),nullptr));
        auto gib=IndexBuffer::Builder().indexCount(6).bufferType(IndexBuffer::IndexType::USHORT).build(*st.engine);
        gib->setBuffer(*st.engine,IndexBuffer::BufferDescriptor(gi,sizeof(gi),nullptr));
        st.groundEntity=EntityManager::get().create();
        RenderableManager::Builder(1).boundingBox({{-s,-0.1f,-s},{s,0.1f,s}})
            .material(0,gmi).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,gvb,gib,0,6)
            .culling(false).receiveShadows(false).castShadows(false).build(*st.engine,st.groundEntity);
        st.scene->addEntity(st.groundEntity); }
    }

    // ThorVG/OUI
    if(tvg::Initializer::init(0)!=tvg::Result::Success){OX_LOG("ThorVG failed\n");return 1;}
    OX::UIManager::loadFont("siyuan","e:/MX/Data/siyuan.ttf");
    st.ui.init(st.app.getGLRC(),(int)W,(int)H);

    // UI
    st.Pw=(float)W; st.Ph=(float)H;
    float Pw=st.Pw, Ph=st.Ph;
    const auto kDim=C(120,120,160), kWhite=C(220,220,230), kGreen=C(0,255,0);

    // Mode switch
    { float mw=180,mh=32; auto bar=std::make_unique<OX::UIFrame>();
        bar->rect=OX::ORect((Pw-mw)/2,6,mw,mh); bar->setStyle(C(24,24,42,220),C(255,255,255,18),1,8);
        bar->setHorizontalLayout(); bar->setPadding(2,2,2,2); bar->setGap(2);
        auto be=std::make_unique<OX::UIButton>("编辑"); be->rect=OX::ORect(0,0,0,mh-4); be->fontSize=12; be->fontName="siyuan"; be->cornerRadius=6;
        be->setColors(C(255,255,255,25),C(255,255,255,35),C(255,255,255,25),kWhite);
        be->onClick=[&](){st.mode=AppMode::Edit;for(auto*e:st.editOnly)e->visible=true;for(auto*e:st.playOnly)e->visible=false;};
        bar->addChild(std::move(be));
        auto bp=std::make_unique<OX::UIButton>("播放"); bp->rect=OX::ORect(0,0,0,mh-4); bp->fontSize=12; bp->fontName="siyuan"; bp->cornerRadius=6;
        bp->setColors(C(0,0,0,0),C(255,255,255,10),C(0,0,0,0),kDim);
        bp->onClick=[&](){st.mode=AppMode::Play;for(auto*e:st.editOnly)e->visible=false;for(auto*e:st.playOnly)e->visible=true;};
        bar->addChild(std::move(bp)); st.ui.addElement(std::move(bar));
    }

    // FPS + Pause
    { auto l=std::make_unique<OX::UILabel>("FPS:0"); l->rect=OX::ORect(8,8,160,22); l->fontSize=13; l->fontName="siyuan"; l->textColor=kGreen; st.fpsLabel=l.get(); st.ui.addElement(std::move(l)); }
    { auto b=std::make_unique<OX::UIButton>("暂停"); b->rect=OX::ORect(8,32,55,22); b->fontSize=11; b->fontName="siyuan";
      b->onClick=[&](){st.autoRotate=!st.autoRotate;st.rotBtn->setText(st.autoRotate?"暂停":"播放");}; st.rotBtn=b.get(); st.ui.addElement(std::move(b)); }

    // Resource panel
    addPanel(st,8,48,200, std::min(Ph-120, 340.0f), "资源库",
        {"- 图片 -","bg_mountain.jpg","sky_panorama.jpg","- 模型 -","pine_tree.glb","- 音频 -","ambient_wind.mp3","- Shader -","ocean_waves.fs","aurora_sky.fs"});

    // Layer panel
    addPanel(st,8, std::min(Ph-120,340.0f)+56, 200, std::max(100.0f,Ph-340-180), "图层",
        {"[X] Textured Cube","[ ] Ground Plane","[ ] Particles","[ ] Audio"});

    // Property panel
    addPanel(st,Pw-208,48,200, std::min(Ph-120, 280.0f), "属性",
        {"- Transform -","Pos [0,0,0]","Rot [0,0,0]","Scl [1,1,1]","- Material -","Texture: LOGO","Filter: Linear"});

    // Toolbar — individual buttons
    {
        float tw = 54*6 + 4*5; // 6 buttons x 54px + 5 gaps x 4px
        float bx = (Pw - tw) / 2, by = Ph - 38;
        auto mkBtn = [&](float x, const char* t, std::function<void()> cb) {
            auto b = std::make_unique<OX::UIButton>(t);
            b->rect = OX::ORect(x, by, 54, 28);
            b->fontSize = 11; b->fontName = "siyuan"; b->cornerRadius = 6;
            b->setColors(C(0,0,0,0), C(255,255,255,20), C(255,255,255,8), kDim);
            if (cb) b->onClick = cb;
            OX::UIElement* bp = b.get();
            st.ui.addElement(std::move(b));
            st.editOnly.push_back(bp);
            return bp;
        };
        mkBtn(bx,       "盒", [&](){ st.objs.push_back(mkBox(st, {0,1.5f,0}, {1,1,1}, {0.3f,0.5f,0.9f,1.0f})); });
        mkBtn(bx+58,    "面", [&](){ st.objs.push_back(mkPlane(st, {3,0,0}, {2,1,2}, {0.2f,0.7f,0.3f,1.0f})); });
        mkBtn(bx+116,   "球", [&](){ st.objs.push_back(mkBox(st, {0,1.5f,3}, {0.8f,0.8f,0.8f}, {0.9f,0.3f,0.3f,1.0f})); });
        mkBtn(bx+174,   "平移", [&](){ st.gop = GTranslate; });
        mkBtn(bx+232,   "旋转", [&](){ st.gop = GRotate; });
        mkBtn(bx+290,   "缩放", [&](){ st.gop = GScale; });
    }

    // Resize
    st.app.setSizeCallback([&](uint32_t w, uint32_t h) {
        st.Pw = (float)w; st.Ph = (float)h;
        if (st.view) st.view->setViewport(filament::Viewport{0, 0, w, h});
        st.ui.handleSize((int)w, (int)h);
        if (st.panels.size() >= 3) {
            st.panels[0]->rect = OX::ORect(8, 48, 200, std::min(st.Ph-120, 340.0f));
            st.panels[1]->rect = OX::ORect(8, std::min(st.Ph-120,340.0f)+56, 200, std::max(100.0f, st.Ph-340-180));
            st.panels[2]->rect = OX::ORect(st.Pw-208, 48, 200, std::min(st.Ph-120, 280.0f));
        }
    });

    // Input
    st.app.setMouseWheelCallback([&](int32_t,int32_t,int d){float f=1.0f-d*0.05f;st.cameraDist=std::clamp(st.cameraDist*f,3.0f,100.0f);});
    st.app.setMouseMoveCallback([&](int32_t x,int32_t y){
        st.mx=x;st.my=y;
        if(st.mode==AppMode::Edit){st.ui.handleMMove(x,y);
            if(st.gDrag&&st.selObj>=0&&st.selObj<(int)st.objs.size()){
                auto&o=st.objs[st.selObj]; float dx=(float)(x-st.gStart.x)*0.02f,dy=(float)(y-st.gStart.y)*0.02f;
                if(st.gop==GTranslate){o.pos.x+=dx;o.pos.y-=dy;}
                else if(st.gop==GScale){o.scl.x=std::max(0.1f,o.scl.x+dx);o.scl.y=std::max(0.1f,o.scl.y-dy);o.scl.z=std::max(0.1f,o.scl.z+dx);}
                auto&t=st.engine->getTransformManager(); t.setTransform(t.getInstance(o.entity),mat4f::translation(o.pos)*mat4f::scaling(o.scl));
                st.gStart={(float)x,(float)y};
            }
        }else st.ui.handleMMove(x,y);
    });
    st.app.setMouseButtonCallback([&](int32_t x,int32_t y,int btn,bool pressed){
        if(btn==0){
            if(st.mode==AppMode::Edit&&pressed&&!st.gDrag){
                st.ui.handleMDown(x,y);
                auto*hit=st.ui.getFocusedElement();
                if(!hit){int best=-1;float bestD=30;
                    for(int i=0;i<(int)st.objs.size();++i){auto&o=st.objs[i];float d=o.pos.x*o.pos.x+o.pos.y*o.pos.y;if(d<bestD){bestD=d;best=i;}}
                    st.selObj=best;st.gDrag=(best>=0);st.gStart={(float)x,(float)y};
                }
            }else if(st.mode==AppMode::Play)st.ui.handleMDown(x,y);
            if(!pressed){st.gDrag=false;st.ui.handleMUp(x,y);}
        }
    });

    st.lastFrameTime=std::chrono::steady_clock::now();st.app.setTargetFPS(0);
    int fc=0;float ft=0;

    // Main loop
    while(!st.app.shouldClose()){
        auto nw=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(nw-st.lastFrameTime).count();st.lastFrameTime=nw;
        st.app.pollEvents();
        fc++;ft+=dt;if(ft>=0.5f){st.fps=fc/ft;char b[64];if(st.selObj>=0)snprintf(b,sizeof(b),"FPS:%.0f [%s]#%d",st.fps,st.gop==GTranslate?"T":st.gop==GScale?"S":"R",st.selObj);else snprintf(b,sizeof(b),"FPS:%.0f",st.fps);st.fpsLabel->setText(b);fc=0;ft=0;}
        if(st.autoRotate)st.cubeAngle+=dt*1.5f;
        auto&t=st.engine->getTransformManager();
        t.setTransform(t.getInstance(st.cubeEntity),mat4f::rotation(st.cubeAngle,float3{0,1,0})*mat4f::rotation(st.cubeAngle*0.5f,float3{1,0,0}));
        float asp=(st.Pw>0&&st.Ph>0)?st.Pw/st.Ph:1.0f;st.camera->setProjection(45.0,asp,0.1,200.0);
        st.camera->lookAt({0,0,st.cameraDist},{0,0,0},{0,1,0});
        if(st.renderer->beginFrame(st.swapChain)){st.renderer->render(st.view);st.renderer->endFrame();}st.engine->flushAndWait();
        wglMakeCurrent(st.app.getDC(),st.app.getGLRC());st.ui.update(dt);st.ui.render();
        if(st.mode==AppMode::Edit&&st.selObj>=0&&st.selObj<(int)st.objs.size()){
            auto&o=st.objs[st.selObj];float dz=st.cameraDist-o.pos.z;if(dz>0.1f){
                float scx=st.Pw*0.5f+o.pos.x*st.Pw*0.8f/dz,scy=st.Ph*0.5f-o.pos.y*st.Ph*0.8f/dz;
                auto*cv=st.ui.getCanvas();if(cv){for(int a=0;a<2;++a){auto s=tvg::Shape::gen();s->appendRect(scx-12-2,scy-2,28,6,2,2);s->fill(a?255:255,a?200:200,0,180);cv->push(std::move(s));s=tvg::Shape::gen();s->appendRect(scx-2,scy-12-2,6,28,2,2);s->fill(a?255:255,a?200:200,0,180);cv->push(std::move(s));}}
            }
        }
        st.app.swapBuffers();st.app.waitForNextFrame();
    }
    st.ui.destroy();tvg::Initializer::term();
    if(st.engine){st.engine->destroy(st.view);st.engine->destroy(st.scene);st.engine->destroy(st.renderer);st.engine->destroy(st.swapChain);st.engine->destroy(st.skybox);st.engine->destroy(st.logoTexture);st.engine->destroy(st.cubeEntity);st.engine->destroy(st.groundEntity);st.engine->destroy(st.vb);st.engine->destroy(st.ib);st.engine->destroy(st.mi);st.engine->destroy(st.mat);st.engine->destroyCameraComponent(st.cameraEntity);EntityManager::get().destroy(st.cameraEntity);Engine::destroy(&st.engine);}
    filamat::MaterialBuilder::shutdown();st.app.destroy();timeEndPeriod(1);return 0;
}
