#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <zspace/zSlicerFieldPreview.h>
#include <iostream>
#include <stdexcept>

using namespace zSpace;
using namespace alice2::slicing;
static void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
static void verifyTexture(unsigned int id, const TexturePixels& expected)
{
    glBindTexture(GL_TEXTURE_2D, id);
    GLint width=0,height=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
    require(width==expected.Width && height==expected.Height,"GPU field resolution changed");
    std::vector<unsigned char> pixels(expected.Rgba.size());
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    require(pixels==expected.Rgba,"GPU field upload transposed or corrupted pixels");
}
static void checks()
{
    zObjMeshScalarField field;
    zFnMeshScalarField(field).create({0,0,0},{3,4,0},3,4,1,true,false);
    zScalarArray path(12),width(12,.4f),palette{.4f};
    for(size_t i=0;i<path.size();++i)path[i]=double(i)-6;
    zTsField::FaceTagSource source;
    zPointArray boundary{{0,0,0},{3,0,0},{3,4,0},{0,4,0}};
    zIntArray edges{0,1,1,2,2,3,3,0};
    zFnGraph(source.boundary).create(boundary,edges);
    source.edgeTags={3,4,5,6};source.edgeWidthIds={0,0,0,0};source.widths={.4f};
    GLuint unpack=0;glGenBuffers(1,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,32,nullptr,GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,17);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glActiveTexture(GL_TEXTURE1);
    SlicerFieldPreview cache;
    const auto& first=cache.layer(0,field,path,width,palette,&source);
    const auto originalTexture=first.Path.Id;
    GLint state=0;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&state);
    require(state==GLint(unpack),"Field upload changed unpack buffer binding");
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&state);require(state==8,"Field upload changed unpack alignment");
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&state);require(state==17,"Field upload changed unpack row length");
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&state);require(state==2,"Field upload changed unpack row offset");
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&state);require(state==3,"Field upload changed unpack column offset");
    glGetIntegerv(GL_ACTIVE_TEXTURE,&state);require(state==GL_TEXTURE1,"Field upload changed the active texture unit");
    glActiveTexture(GL_TEXTURE0);
    zColorArray colors;zTsUtils::colorPathField(path,colors);
    verifyTexture(first.Path.Id,fieldPixels(cache.grid(),colors));
    zTsUtils::colorWidthField(width,colors,&palette);
    verifyTexture(first.Width.Id,fieldPixels(cache.grid(),colors));
    zIntArray tags;require(zTsField::queryFaceTags(source,cache.grid().Positions,tags),"Tag fixture query failed");
    colors.clear();for(int tag:tags)colors.push_back(slicerFaceTagColor(tag));
    verifyTexture(first.Tags.Id,fieldPixels(cache.grid(),colors,true));
    glBindTexture(GL_TEXTURE_2D,first.Tags.Id);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&state);
    require(state==GL_NEAREST,"Categorical field interpolates FaceTag IDs");
    for(int i=0;i<20;++i)
        require(cache.layer(0,field,path,width,palette,&source).Path.Id==originalTexture,"Revisited layer rebuilt texture");
    require(cache.buildCount()==1,"Cache hit repeated field queries or uploads");
    for(int i=1;i<=128;++i)cache.layer(i,field,path,width,palette,nullptr);
    require(cache.size()==128 && cache.bytes()<=64*1024*1024,"Field cache is not bounded");
    require(cache.buildCount()==129,"Cold layer did not build exactly once");
    cache.layer(0,field,path,width,palette,&source);
    require(cache.buildCount()==130,"Oldest layer was not evicted");
    const GLuint released=cache.layer(0,field,path,width,palette,&source).Path.Id;
    cache.clear();require(cache.size()==0 && cache.bytes()==0 && !glIsTexture(released),"Cache invalidation leaked a GL texture");
    zObjMeshScalarField changed;
    zFnMeshScalarField(changed).create({0,0,0},{2,5,0},2,5,1,true,false);
    zScalarArray nextPath(10,1),nextWidth(10,.4f);
    cache.layer(0,changed,nextPath,nextWidth,palette,nullptr);
    require(cache.grid().Nx==2 && cache.grid().Ny==5 && cache.buildCount()==1,"New solve reused stale grid data");
    cache.clear();glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&unpack);
}
int main()
{
    if(!glfwInit()){std::cout<<"SKIP no GLFW context\n";return 77;}
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_COMPAT_PROFILE);
    GLFWwindow* window=glfwCreateWindow(64,64,"Inspector verification",nullptr,nullptr);
    if(!window){glfwTerminate();std::cout<<"SKIP no compatible GL context\n";return 77;}
    glfwMakeContextCurrent(window);
    int result=0;
    if(glewInit()!=GLEW_OK)result=77;
    else try{checks();std::cout<<"PASS field GPU upload, cache reuse, eviction and invalidation\n";}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';result=1;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
