#include "Test.h"
#include "fvr/runtime/ExactWriteBatch.h"
using fvr::runtime::ExactWriteBatch;
int main(){
    std::array<std::byte,8> a{},b{},one{},two{};one.fill(std::byte{1});two.fill(std::byte{2});
    {ExactWriteBatch<2> scope;CHECK(scope.Stage(a,a,one));CHECK(scope.Stage(b,b,two));CHECK(scope.Apply());CHECK(a==one&&b==two);CHECK(!scope.Apply());CHECK(!scope.Stage(a,one,two));CHECK(scope.Restore());CHECK(a==b);CHECK(!scope.Apply());}
    // A changed second target must prevent every write, including the first.
    {ExactWriteBatch<2> scope;CHECK(scope.Stage(a,a,one));CHECK(scope.Stage(b,b,two));b[7]=std::byte{3};CHECK(!scope.Apply());CHECK(a[0]==std::byte{}&&b[7]==std::byte{3});b={};}
    // Preserve a subsequent native write while restoring the other target.
    {ExactWriteBatch<2> scope;CHECK(scope.Stage(a,a,one));CHECK(scope.Stage(b,b,two));CHECK(scope.Apply());b[4]=std::byte{9};CHECK(!scope.Restore());CHECK((a==std::array<std::byte,8>{}));CHECK(b[4]==std::byte{9}&&b[0]==std::byte{2});b={};}
    {ExactWriteBatch<2> scope;CHECK(scope.Stage(a,a,one));CHECK(!scope.Stage(std::span(a).subspan(4),std::span(a).subspan(4),std::span(one).subspan(4)));CHECK(scope.Apply());}
    CHECK(a==b); // Destructor restores after an early scope exit.
    {ExactWriteBatch<1,4> scope;CHECK(!scope.Stage(a,a,one));CHECK(!scope.Stage({}, {}, {}));CHECK(!scope.Apply());}
    // Preallocated reuse: Reset is refused while live and reopens a finished scope.
    {static ExactWriteBatch<2> pooled;CHECK(pooled.Stage(a,a,one));CHECK(pooled.Overlaps(a.data()+7,1));CHECK(!pooled.Overlaps(b.data(),8));
        CHECK(pooled.Apply());CHECK(!pooled.Reset());CHECK(pooled.Restore());CHECK(a==b);CHECK(pooled.Reset());CHECK(pooled.Count()==0);
        CHECK(!pooled.Overlaps(a.data(),8));CHECK(pooled.Stage(b,b,two));CHECK(pooled.Apply());CHECK(b==two);CHECK(pooled.Restore());CHECK(pooled.Reset());
        CHECK((b==std::array<std::byte,8>{}));}
    return 0;
}
