// SPDX-License-Identifier: MIT
// Exact analytical surface oracle for tests/previews only. Production obtains
// whole-patch certificates from the host's current renderer-selected mesh.
#pragma once
#include "telegraphs_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bastion::telegraphs::fixture {
struct Point { double x, y; };
using Polygon = std::vector<Point>;
struct Region {
    Polygon polygon; double x_slope = 0, y_slope = 0, height = 0; Polygon source;
    Region(Polygon shape,double sx=0,double sy=0,double z=0,Polygon original={}) : polygon(std::move(shape)),x_slope(sx),y_slope(sy),height(z),source(std::move(original)) {}
};
inline double side(Point a, Point b, Point p) { return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x); }
inline double area(const Polygon& polygon) {
    double value = 0;
    for (std::size_t n=0;n<polygon.size();++n) { const auto a=polygon[n],b=polygon[(n+1)%polygon.size()]; value+=a.x*b.y-a.y*b.x; }
    return std::abs(value)*.5;
}
inline void counterclockwise(Polygon& polygon) {
    double winding=0;for(std::size_t n=0;n<polygon.size();++n) {const auto a=polygon[n],b=polygon[(n+1)%polygon.size()];winding+=a.x*b.y-a.y*b.x;}
    if(winding<0) std::reverse(polygon.begin(),polygon.end());
}
template<class Distance> inline Polygon clip_distance(const Polygon& polygon,Distance distance) {
    Polygon output;
    if (polygon.empty()) return output;
    auto previous=polygon.back(); double prior=distance(previous);
    for (const auto current:polygon) {
        const double next=distance(current);
        if ((prior>=0)!=(next>=0)) {
            const double fraction=prior/(prior-next);
            output.push_back({previous.x+(current.x-previous.x)*fraction,previous.y+(current.y-previous.y)*fraction});
        }
        if (next>=0) output.push_back(current);
        previous=current;prior=next;
    }
    return output;
}
inline Polygon half_plane(const Polygon& polygon, Point a, Point b, bool inside=true) {
    return clip_distance(polygon,[&](Point point){return side(a,b,point)*(inside?1:-1);});
}
inline Polygon intersection(Polygon polygon,const Polygon& mask) {
    for(std::size_t n=0;n<mask.size();++n) polygon=half_plane(polygon,mask[n],mask[(n+1)%mask.size()]);
    return polygon;
}
inline std::vector<Polygon> difference(Polygon polygon,const Polygon& mask) {
    std::vector<Polygon> output;
    for(std::size_t n=0;n<mask.size()&&!polygon.empty();++n) {
        auto outside=half_plane(polygon,mask[n],mask[(n+1)%mask.size()],false);
        if(area(outside)>1e-12) output.push_back(std::move(outside));
        polygon=half_plane(polygon,mask[n],mask[(n+1)%mask.size()]);
    }
    return output;
}
inline Polygon rectangle(double left,double right,double back,double front) { return {{left,back},{right,back},{right,front},{left,front}}; }
inline Polygon box(double x,double y,double length,double width,double angle) {
    auto polygon=rectangle(-length*.5,length*.5,-width*.5,width*.5);
    for(auto& point:polygon) { const double px=point.x;point.x=static_cast<float>(x+px*std::cos(angle)-point.y*std::sin(angle));point.y=static_cast<float>(y+px*std::sin(angle)+point.y*std::cos(angle)); }
    return polygon;
}
inline SurfacePatchSample certify(const std::array<TriangleVertex,3>& vertices,const std::vector<Region>& regions,float maximum_rise=6) {
    const Polygon input={{vertices[0].x,vertices[0].y},{vertices[1].x,vertices[1].y},{vertices[2].x,vertices[2].y}};
    const double determinant=side(input[0],input[1],input[2]);
    if(determinant==0) return {};
    const double region_epsilon=std::max(1e-15,area(input)*1e-10);
    const auto plane=[&](Point p) {
        const double a=side(input[1],input[2],p)/determinant,b=side(input[2],input[0],p)/determinant;
        return a*vertices[0].z+b*vertices[1].z+(1-a-b)*vertices[2].z;
    };
    std::vector<Region> bounded;
    for(const auto& region:regions) {
        const auto height=[&](Point point){return point.x*region.x_slope+point.y*region.y_slope+region.height;};
        const auto clipped=intersection(input,region.polygon);
        auto visible=clip_distance(clipped,[&](Point point){return plane(point)+maximum_rise-height(point);});
        counterclockwise(visible);
        if(area(visible)>region_epsilon) bounded.push_back({std::move(visible),region.x_slope,region.y_slope,region.height,region.source.empty()?region.polygon:region.source});
        auto over=clip_distance(clipped,[&](Point point){return height(point)-plane(point)-maximum_rise;});
        counterclockwise(over);
        if(area(over)>region_epsilon) bounded.push_back({std::move(over),region.x_slope,region.y_slope,0,rectangle(-20000,20000,-20000,20000)});
    }
    for(const auto& region:bounded) {
        const auto clipped=intersection(input,region.polygon);
        if(area(clipped)<region_epsilon) continue;
        Point center{};for(const auto point:clipped) {center.x+=point.x;center.y+=point.y;}center.x/=static_cast<double>(clipped.size());center.y/=static_cast<double>(clipped.size());
        const auto height=[&](Point point){return point.x*region.x_slope+point.y*region.y_slope+region.height;};
        // Difference is affine on this exact region. Move towards a differing
        // vertex if its centroid happens to lie on the two planes' crossing.
        Point witness=center;
        if(std::abs(height(witness)-plane(witness))<=.001) {
            for(const auto point:clipped) if(std::abs(height(point)-plane(point))>.003) {witness={(point.x+center.x)*.5,(point.y+center.y)*.5};break;}
        }
        if(std::abs(height(witness)-plane(witness))>.002) {
            SurfacePatchSample result{PatchStatus::Mismatch,static_cast<float>(witness.x),static_cast<float>(witness.y),static_cast<float>(height(witness)),true};
            if(clipped.size()<=result.region.size()) {
                result.region_count=static_cast<std::uint32_t>(clipped.size());
                for(std::size_t n=0;n<clipped.size();++n) result.region[n]={clipped[n].x,clipped[n].y};
            }
            const auto& source=region.source.empty()?region.polygon:region.source;
            double best=-1e100;
            for(std::size_t n=1;n+1<source.size();++n) {
                const std::array<Point,3> face={source[0],source[n],source[n+1]};
                const double determinant_face=side(face[0],face[1],face[2]);
                if(std::abs(determinant_face)<1e-10) continue;
                const double interior=std::min({side(face[0],face[1],witness),side(face[1],face[2],witness),side(face[2],face[0],witness)})/determinant_face;
                if(interior<best) continue;
                best=interior;
                result.face_known=true;
                for(std::size_t v=0;v<3;++v) result.face[v]={static_cast<float>(face[v].x),static_cast<float>(face[v].y),static_cast<float>(height(face[v])),0};
            }
            return result;
        }
    }
    return {PatchStatus::Planar};
}
inline std::vector<Region> raised_props(const std::vector<Region>& props) {
    std::vector<Polygon> ground={rectangle(-20000,20000,-20000,20000)};
    for(const auto& prop:props) {
        std::vector<Polygon> remainder;
        for(auto& polygon:ground) {
            auto pieces=difference(std::move(polygon),prop.polygon);
            remainder.insert(remainder.end(),std::make_move_iterator(pieces.begin()),std::make_move_iterator(pieces.end()));
        }
        ground=std::move(remainder);
    }
    auto output=props;
    for(auto& polygon:ground) output.push_back({std::move(polygon),0,0,0,rectangle(-20000,20000,-20000,20000)});
    return output;
}
inline SurfacePatchSample certify_props(const std::array<TriangleVertex,3>& vertices,const std::vector<Region>& props,float maximum_rise=6) {
    Polygon input={{vertices[0].x,vertices[0].y},{vertices[1].x,vertices[1].y},{vertices[2].x,vertices[2].y}};
    counterclockwise(input);
    std::vector<Polygon> ground={input};
    for(const auto& prop:props) {
        std::vector<Polygon> remainder;
        for(auto& polygon:ground) {
            auto pieces=difference(std::move(polygon),prop.polygon);
            remainder.insert(remainder.end(),std::make_move_iterator(pieces.begin()),std::make_move_iterator(pieces.end()));
        }
        ground=std::move(remainder);
    }
    auto regions=props;
    for(auto& polygon:ground) regions.emplace_back(std::move(polygon),0,0,0,rectangle(-20000,20000,-20000,20000));
    return certify(vertices,regions,maximum_rise);
}
} // namespace bastion::telegraphs::fixture
