#include "PM.h"
#include <cassert>
#include <vector>
#include <fstream>

#ifdef __APPLE__

#include <OpenGL/OpenGL.h>
#include <GLUT/glut.h>
#include "MeshLib_Core/Mesh.h"
#include "MeshLib_Core/Iterators.h"

#else
#include <GL/glut.h>
#include "MeshLib_Core\Mesh.h"
#include "MeshLib_Core\Iterators.h"
#include "Utils\Heaps\Fib_Heap.h"
#endif
#include "XLibCommon.h"

using namespace XMeshLib;

void PM::SetEdgePriority() {

}

bool PM::CheckEdgeCollapseCondition(Edge * e) {
    Halfedge *he = e->he(0);
    Vertex *v1 = he->source();
    Vertex *v2 = he->target();
    std::vector<int> set1;
    std::vector<int> set2;
    for (VertexVertexIterator vvit(v1); !vvit.end(); ++vvit) {
        Vertex *vv1 = *vvit;
        set1.push_back(vv1->index());
    }
    for (VertexVertexIterator vvit(v2); !vvit.end(); ++vvit) {
        Vertex *vv2 = *vvit;
        set2.push_back(vv2->index());
    }
    std::sort(set1.begin(), set1.end());
    std::sort(set2.begin(), set2.end());
    std::vector<int> intSet(set1.size() + set2.size());
    std::vector<int>::iterator it = std::set_intersection(set1.begin(), set1.end(), set2.begin(), set2.end(), intSet.begin());
    intSet.resize(it - intSet.begin());

    if (intSet.size() == 2)
        return true;
    return false;
}

Vertex *PM::EdgeCollapse(Edge *e, VSplitRecord &vsRec) {
    Halfedge *phe[6];
    phe[0] = e->he(0);
    phe[1] = e->he(1);
    assert(phe[1]);
    Face *f1 = phe[0]->face();
    Face *f2 = phe[1]->face();
    phe[2] = phe[0]->next();
    phe[4] = phe[0]->prev();
    phe[3] = phe[1]->prev();
    phe[5] = phe[1]->next();
    Halfedge *dhe2 = phe[2]->twin();
    Halfedge *dhe3 = phe[3]->twin();
    Halfedge *dhe4 = phe[4]->twin();
    Halfedge *dhe5 = phe[5]->twin();
    Vertex *vs = phe[0]->target();
    Vertex *vt = phe[1]->target();
    Vertex *vl = phe[2]->target();
    Vertex *vr = phe[5]->target();
    //merge vt to vs
    //(1) link all vt's neighboring halfedge to vs
    std::vector<Halfedge *> tmpHe;
    for (VertexInHalfedgeIterator hit(vt); !hit.end(); ++hit) {
        Halfedge *he = *hit;
        if (he == phe[1])
            continue;
        tmpHe.push_back(he);
    }
    for (unsigned int i = 0; i < tmpHe.size(); ++i)
        tmpHe[i]->target() = vs;

    //(2) set the new tiwns halfedges of dhe2 and dhe3 to dhe4 and dhe5, respectively
    Edge *e1 = dhe2->edge();
    Edge *e2 = dhe3->edge();
    Edge *de1 = dhe4->edge();
    Edge *de2 = dhe5->edge();
    e1->he(0) = dhe2;
    e1->he(1) = dhe4;
    dhe4->edge() = e1;
    e2->he(0) = dhe3;
    e2->he(1) = dhe5;
    dhe5->edge() = e2;

    //(3) set related vertices' halfedges
    vs->he() = dhe2;
    vl->he() = dhe4;
    vr->he() = dhe3;

    //(4) delete he[0~6], delete the two faces, and delete three edges
    DeleteFace(f1);
    DeleteFace(f2);

    vsRec.vs = vs;
    vsRec.vl = vl;
    vsRec.vr = vr;
    vsRec.vt = vt;
    vsRec.old_vs_pt = vs->point();
    vs->point() = (vs->point() + vt->point()) / 2;
    //DeleteVertex(vt);
    tMesh->m_verts[vt->index()] = NULL;
    DeleteEdge(e);
    DeleteEdge(de1);
    DeleteEdge(de2);
    for (int i = 0; i < 6; ++i)
        delete phe[i];

    return NULL;
}

void PM::VertexSplit(VSplitRecord &vsRec) {

    //(0) Obtain prestored primitives from vsplitRecord, and undeleted primitives from halfedge data structure
    Vertex *vt = vsRec.vt;
    Vertex *vs = vsRec.vs;    //tMesh->indVertex(vsRec.vs_ind);
    Vertex *vl = vsRec.vl;    //tMesh->indVertex(vsRec.vl_ind);
    Vertex *vr = vsRec.vr;    //tMesh->indVertex(vsRec.vr_ind);
    Halfedge *dhe4 = tMesh->vertexHalfedge(vs, vl);
    Halfedge *dhe3 = tMesh->vertexHalfedge(vs, vr);
    Halfedge *dhe2 = dhe4->twin();
    Halfedge *dhe5 = dhe3->twin();
    vs->point() = vsRec.old_vs_pt;

    // (2) Create new primitives
    assert(!tMesh->m_verts[vt->index()]);
    tMesh->m_verts[vt->index()] = vt;
    Halfedge *phe[6];
    for (int i = 0; i < 6; ++i) {
        phe[i] = new Halfedge;
    }
    Edge *e_sl = dhe2->edge();
    Edge *e_sr = dhe3->edge();
    Edge *e_tl = CreateEdge(dhe4, phe[4]);
    Edge *e_tr = CreateEdge(dhe5, phe[5]);
    Edge *e_st = CreateEdge(phe[0], phe[1]);
    Face *f0 = CreateFace();
    Face *f1 = CreateFace();

    //(2) Collect following ccw order, about vt, all in halfedges from dhe4->prev() to dhe5. These he's target should be linked to vt
    Halfedge *che = dhe4->prev();
    std::vector<Halfedge *> cheList;
    while (che != dhe5) {
        cheList.push_back(che);
        che = che->ccw_rotate_about_target();
    };
    cheList.push_back(dhe5);

    //(3) Link primitives
    // a. between halfedges
    phe[0]->next() = phe[2];
    phe[0]->prev() = phe[4];
    phe[2]->next() = phe[4];
    phe[2]->prev() = phe[0];
    phe[4]->next() = phe[0];
    phe[4]->prev() = phe[2];
    phe[1]->next() = phe[5];
    phe[1]->prev() = phe[3];
    phe[3]->next() = phe[1];
    phe[3]->prev() = phe[5];
    phe[5]->next() = phe[3];
    phe[5]->prev() = phe[1];
    // b. between halfedges and edges
    e_sl->he(0) = dhe2;
    e_sl->he(1) = phe[2];
    e_sr->he(0) = dhe3;
    e_sr->he(1) = phe[3];
    //e_tl->he(0) = dhe4;
    //e_tl->he(1) = phe[4];
    //e_tr->he(0) = dhe5;
    //e_tr->he(1) = phe[5];
    //e_st->he(0) = phe[0];
    //e_st->he(1) = phe[1];
    phe[2]->edge() = e_sl;
    phe[3]->edge() = e_sr;
    phe[0]->edge() = e_st;
    phe[1]->edge() = e_st;
    phe[4]->edge() = e_tl;
    phe[5]->edge() = e_tr;
    dhe4->edge() = e_tl;
    dhe5->edge() = e_tr;

    // c. between halfedges and vertices
    vs->he() = dhe2;
    vt->he() = dhe5;
    vl->he() = dhe4;
    vr->he() = dhe3;
    phe[0]->target() = vs;
    phe[2]->target() = vl;
    phe[4]->target() = vt;
    phe[1]->target() = vt;
    phe[3]->target() = vs;
    phe[5]->target() = vr;
    for (unsigned int i = 0; i < cheList.size(); ++i)
        cheList[i]->target() = vt;
    // d. between halfedges and faces
    phe[0]->face() = phe[2]->face() = phe[4]->face() = f0;
    phe[1]->face() = phe[3]->face() = phe[5]->face() = f1;
    f0->he() = phe[0];
    f1->he() = phe[1];
}


Face *PM::CreateFace() {
    Face *f = new Face();
    int fNum = tMesh->m_faces.size();
    if (!fNum)
        f->index() = 0;
    else
        f->index() = tMesh->m_faces[fNum - 1]->index() + 1;
    tMesh->m_faces.push_back(f);
    return f;
}

Vertex *PM::CreateVertex() {
    Vertex *v = new Vertex();
    int vNum = tMesh->m_verts.size();
    if (!vNum)
        v->index() = 0;
    else
        v->index() = tMesh->m_verts[vNum - 1]->index() + 1;
    tMesh->m_verts.push_back(v);
    return v;
}

Edge *PM::CreateEdge(Halfedge * he0, Halfedge * he1) {
    Edge *e = new Edge(he0, he1);
    int eNum = tMesh->m_edges.size();
    if (!eNum)
        e->index() = 0;
    else
        e->index() = tMesh->m_edges[eNum - 1]->index() + 1;
    tMesh->m_edges.push_back(e);
    return e;
}

void PM::DeleteFace(Face * f) {
    std::vector<Face *> &fList = tMesh->m_faces;
    std::vector<Face *>::iterator fPos = std::find(fList.begin(), fList.end(), f);
    assert(fPos != fList.end());
    fList.erase(fPos);
    delete f;
}

void PM::DeleteVertex(Vertex * v) {
    std::vector<Vertex *> &vList = tMesh->m_verts;
    std::vector<Vertex *>::iterator vPos = std::find(vList.begin(), vList.end(), v);
    assert(vPos != vList.end());
    vList.erase(vPos);
    delete v;
}

void PM::DeleteEdge(Edge * e) {
    std::vector<Edge *> &eList = tMesh->m_edges;
    std::vector<Edge *>::iterator ePos = std::find(eList.begin(), eList.end(), e);
    assert(ePos != eList.end());
    eList.erase(ePos);
    delete e;
}

void PM::printE(Edge * e) {
    printHE(e->he(0));
    printHE(e->he(1));
}

void PM::printHE(Halfedge * he) {
    if (he == NULL)
        std::cout << "NULL \n";
    else
        std::cout << "src: " << he->source()->index() << " trg: " << he->target()->index() << "\n";
}

bool PM::WriteVsplitRecord(const char filename[], std::vector<VSplitRecord> &vsRecList) {
    std::ofstream output(filename);
    if (!output.good()) {
        std::cerr << "Can't open file " << filename << "!" << std::endl;
        return false;
    }
    output << vsRecList.size() << "\n";
    for (unsigned int i = 0; i < vsRecList.size(); ++i) {
        VSplitRecord &vRec = vsRecList[i];
        output << vRec.vs->index() + 1 << " " << vRec.vt->index() + 1 << " " << vRec.vl->index() + 1 << " " << vRec.vr->index() + 1 << "\n";
    }
    output.close();
    return true;
}

bool PM::SaveMesh(const char filename[]) {
    std::cout << "Writing mesh " << filename << " ...";
    std::ofstream output(filename);
    if (!output.good()) {
        std::cerr << "Can't open file " << filename << "!" << std::endl;
        return false;
    }

    int vSize = tMesh->numVertices();
    int fSize = tMesh->numFaces();
    int eSize = tMesh->numEdges();

    for (int i = 0; i < vSize; ++i) {
        Vertex *v = tMesh->m_verts[i];
        if (!v) continue;
//        output << "Vertex " << v->index() + 1 << " " << v->point();
        if (!v->PropertyStr().empty())
            output << " {" << v->PropertyStr() << "}";
        output << "\n";
    }
    for (int i = 0; i < fSize; ++i) {
        Face *f = tMesh->m_faces[i];
        Halfedge *the0 = f->he();
        Halfedge *the1 = the0->next();
        int vid0 = the0->source()->index() + 1;
        int vid1 = the0->target()->index() + 1;
        int vid2 = the1->target()->index() + 1;
        output << "Face " << f->index() + 1 << " " << vid0 << " " << vid1 << " " << vid2;
        if (!f->PropertyStr().empty())
            output << " {" << f->PropertyStr() << "}";
        output << "\n";
    }

    for (int i = 0; i < eSize; ++i) {
        Edge *e = tMesh->m_edges[i];
        int vid0 = e->he(0)->source()->index() + 1;
        int vid1 = e->he(0)->target()->index() + 1;
        if (!e->PropertyStr().empty()) {
            output << "Edge " << vid0 << " " << vid1 << " {" << e->PropertyStr() << "}\n";
        }
    }

    output.close();

    std::cout << "Done!" << std::endl;
    return true;
}

Edge *PM::GetNextCollapseEdge() {
    std::pair<Edge *, double> minE;
    minE.first = NULL;
    minE.second = 1e10;
    for (MeshEdgeIterator eit(tMesh); !eit.end(); ++eit) {
        Edge *e = *eit;
        Point &p0 = e->he(0)->target()->point();
        Point &p1 = e->he(1)->target()->point();
        double clen = (p0 - p1).norm();
        if (clen < minE.second) {
            bool canCollapse = CheckEdgeCollapseCondition(e);
            if (canCollapse) {
                minE.second = clen;
                minE.first = e;
            }
        }
    }
    if (!minE.first)
        return NULL;
    else
        return minE.first;
}

void PM::ProcessCoarsening(int targetVertSize) {
    for (baseMeshResolution = tMesh->numVertices(); baseMeshResolution > targetVertSize; --baseMeshResolution) {
        Edge *cE = GetNextCollapseEdge();
        if (!cE)
            break;
        XMeshLib::VSplitRecord vsRec;
        EdgeCollapse(cE, vsRec);
        vsRecList.push_back(vsRec);
        //std::cout << "after " << iter << "iterations:";
        //std::string fname = GenerateIndexedFileName("coarse", iterNum-iter-1, ".m");
        //SaveTemporaryMesh(fname.c_str());
    }
    SaveMesh("baseMesh.m");
    WriteVsplitRecord("vSplitRecord.txt", vsRecList);
}

void PM::ProcessRefinement(int targetVertSize) {
    unsigned int iterNum;
    if (targetVertSize == -1)
        iterNum = vsRecList.size();
    else {
        iterNum = targetVertSize - baseMeshResolution;
        if (iterNum > vsRecList.size())
            iterNum = vsRecList.size();
    }
    for (int iter = iterNum - 1; iter >= 0; --iter) {
        XMeshLib::VSplitRecord &vsRec = vsRecList[iter];
        VertexSplit(vsRec);
        //std::string fname = GenerateIndexedFileName("refine", iterNum-iter, ".m");
        //SaveTemporaryMesh(fname.c_str());
        vsRecList.pop_back();
    }
    std::string fname = GenerateIndexedFileName("refine", (baseMeshResolution + iterNum), ".m");
    SaveMesh(fname.c_str());
}


void PM::GetValidTmpMesh() {
    if (tmpMesh)
        delete tmpMesh;
    tmpMesh = new Mesh;
    tmpInd2OInd.clear();

    for (std::vector<Vertex *>::iterator viter = tMesh->m_verts.begin(); viter != tMesh->m_verts.end(); ++viter) {
        Vertex *v = *viter;
        Vertex *nv = tmpMesh->createVertex();
        if (!v) {
            nv->PropertyStr() = "invalid";
            continue;
        }
        nv->point() = v->point();
        nv->PropertyStr() = v->PropertyStr();
        nv->boundary() = v->boundary();
    }

    std::vector<Face *>::iterator fiter = tMesh->m_faces.begin();
    int cfind = 0;
    for (; fiter != tMesh->m_faces.end(); ++fiter) {
        Face *f = *fiter;
        f->index() = cfind++;
        Face *nf = tmpMesh->createFace();
        Halfedge *he[3];
        Halfedge *nhe[3];
        he[0] = f->he();
        he[1] = he[0]->next();
        he[2] = he[1]->next();
        for (int j = 0; j < 3; ++j) {
            nhe[j] = new Halfedge();
            Vertex *v1 = he[j]->target();
            Vertex *nv1 = tmpMesh->indVertex(v1->index());
            nv1->he() = nhe[j];
            nhe[j]->target() = nv1;
            nhe[j]->face() = nf;
        }
        for (int j = 0; j < 3; ++j) {
            nhe[j]->next() = nhe[(j + 1) % 3];
            nhe[j]->prev() = nhe[(j + 2) % 3];
        }
        nf->he() = nhe[0];
        nf->PropertyStr() = f->PropertyStr();
    }

    for (fiter = tMesh->m_faces.begin(); fiter != tMesh->m_faces.end(); ++fiter) {
        Face *f = *fiter;
        Face *nf = tmpMesh->indFace(f->index());
        Halfedge *he[3];
        Halfedge *nhe[3];
        he[0] = f->he();
        he[1] = he[0]->next();
        he[2] = he[1]->next();
        nhe[0] = nf->he();
        nhe[1] = nhe[0]->next();
        nhe[2] = nhe[1]->next();
        for (int i = 0; i < 3; ++i) {
            Edge *e = he[i]->edge();
            if (he[i] == e->he(0)) {
                if (e->boundary()) {
                    Edge *ne = tmpMesh->createEdge(nhe[i], NULL);
                    nhe[i]->edge() = ne;
                    ne->PropertyStr() = e->PropertyStr();
                    continue;
                }
                //get its twin: twin_he
                Halfedge *twin_he, *twin_nhe;
                Face *tf = he[i]->twin()->face();
                Face *tnf = tmpMesh->indFace(tf->index());
                twin_he = tf->he();
                twin_nhe = tnf->he();
                for (int j = 0; j < 3; ++j) {
                    if (twin_he->edge() == e) break;
                    twin_he = twin_he->next();
                    twin_nhe = twin_nhe->next();
                }
                Edge *ne = tmpMesh->createEdge(nhe[i], twin_nhe);
                nhe[i]->edge() = ne;
                twin_nhe->edge() = ne;
                ne->PropertyStr() = e->PropertyStr();
            }
        }
    }
    for (std::vector<Vertex *>::iterator vit = tmpMesh->m_verts.begin(); vit != tmpMesh->m_verts.end();) {
        Vertex *v = *vit;
        if (v->PropertyStr().compare("invalid") == 0) {
            vit = tmpMesh->m_verts.erase(vit);
        }
        else {
            tmpInd2OInd.push_back(v->index());
            v->index() = tmpInd2OInd.size() - 1;
            ++vit;
        }
    }
}