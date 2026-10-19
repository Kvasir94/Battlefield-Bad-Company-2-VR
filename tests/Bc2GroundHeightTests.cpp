#include "Bc2GroundHeight.h"
#include "Test.h"
#include <cmath>

using namespace fvr;
namespace {
// Native SP_02 shape: right-handed standard-Z projection (runs 205643/211951) and a
// camera 1.575 m over a flat floor at y 173.71.
math::Matrix4 Projection(){math::Matrix4 p{};p.values[0][0]=1;p.values[1][1]=1.25f;p.values[2][2]=-1.00002f;p.values[2][3]=-1;p.values[3][2]=-.100002f;return p;}
// Camera at (cx,cy,cz), yaw about +Y then pitch about the turned +X (negative = down).
math::Matrix4 View(float cx,float cy,float cz,float yaw,float pitch){
    const float cyw=std::cos(yaw),syw=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    // Camera-to-world rows (row-vector): right, up, back.
    const float r[3]={cyw,0,-syw},u[3]={syw*sp,cp,cyw*sp},b[3]={syw*cp,-sp,cyw*cp};
    math::Matrix4 v{};for(int i=0;i<3;++i){v.values[i][0]=r[i];v.values[i][1]=u[i];v.values[i][2]=b[i];}
    v.values[3][0]=-(cx*r[0]+cy*r[1]+cz*r[2]);v.values[3][1]=-(cx*u[0]+cy*u[1]+cz*u[2]);v.values[3][2]=-(cx*b[0]+cy*b[1]+cz*b[2]);v.values[3][3]=1;return v;
}
// Depth of a plane y=floor seen through pixel (x,y) of a width x height target.
struct Floor {
    math::Matrix4 view,projection;unsigned width,height;float floorY;float wallDepth=-1;
    float operator()(unsigned px,unsigned py)const{
        if(wallDepth>=0)return wallDepth;
        const float nx=(float(px)+.5f)/float(width)*2-1,ny=1-(float(py)+.5f)/float(height)*2;
        const auto inv=interaction::InverseRigid(view);const auto& i=inv->values;
        const float d[3]={nx/projection.values[0][0],ny/projection.values[1][1],-1}; // view-space ray
        float w[3];for(int k=0;k<3;++k)w[k]=d[0]*i[0][k]+d[1]*i[1][k]+d[2]*i[2][k];
        if(!(w[1]<0))return 1.f; // sky
        const float t=(floorY-i[3][1])/w[1];const float vz=-t; // view z of the hit
        return (vz*projection.values[2][2]+projection.values[3][2])/-vz;
    }
};
} // namespace

int main(){
    const auto p=Projection();
    // Level camera: the nearest on-screen candidate is ahead; the floor comes back to ~1 cm.
    {const auto v=View(243.4f,175.285f,-148.47f,.3f,0);Floor f{v,p,1024,768,173.71f};
     const auto s=bc2::ground::GroundFromDepth(v,p,1024,768,f);CHECK(s&&std::abs(s->y-173.71f)<.02f&&s->distance>1.f);}
    // Looking down 60 degrees: a near candidate is visible and wins.
    {const auto v=View(243.4f,175.285f,-148.47f,-.5f,-1.047f);Floor f{v,p,1024,768,173.71f};
     const auto s=bc2::ground::GroundFromDepth(v,p,1024,768,f);CHECK(s&&std::abs(s->y-173.71f)<.02f&&s->distance<1.2f);}
    // A wall right in front (depth of ~0.5 m everywhere) is not ground; sky is not either.
    {const auto v=View(0,10,0,0,0);Floor f{v,p,1024,768,8.4f};f.wallDepth=(-.5f*-1.00002f-.100002f)/.5f;
     CHECK(!bc2::ground::GroundFromDepth(v,p,1024,768,f));f.wallDepth=1.f;CHECK(!bc2::ground::GroundFromDepth(v,p,1024,768,f));}
    // Straight down has no forward direction to aim along.
    {const auto v=View(0,10,0,0,-1.5707f);Floor f{v,p,1024,768,8.4f};CHECK(!bc2::ground::GroundFromDepth(v,p,1024,768,f));}
    // A point 2 m straight ahead lands mid-screen and unprojects back to itself.
    {const auto v=View(1,2,3,0,0);const auto px=bc2::ground::ProjectToPixel(v,p,1,2,1,1024,768);CHECK(px&&px->x==512&&px->y==384);
     const auto w=bc2::ground::Unproject(v,p,px->ndcX,px->ndcY,(-2*-1.00002f-.100002f)/2);CHECK(w&&std::abs((*w)[2]-1)<.01f&&std::abs((*w)[1]-2)<.01f);}
    // Publication: nothing before the first sample; fresh within the age bound only.
    CHECK(!bc2::ground::Latest(1000,500));
    bc2::ground::Publish({173.71f,1,2,1.5f,1000,7});
    const auto latest=bc2::ground::Latest(1200,500);CHECK(latest&&latest->y==173.71f&&latest->frame==7);
    CHECK(!bc2::ground::Latest(1600,500));CHECK(!bc2::ground::Latest(900,500));
    return 0;
}
