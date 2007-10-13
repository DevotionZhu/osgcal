/* -*- c++ -*-
    Copyright (C) 2003 <ryu@gpul.org>
    Copyright (C) 2006 Vladimir Shabanov <vshabanoff@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <cal3d/model.h>

#include <osg/BlendFunc>
#include <osg/Image>
#include <osg/Material>
#include <osg/NodeCallback>
#include <osg/ShapeDrawable>
#include <osg/TexEnv>
#include <osg/TexGen>
#include <osg/TexEnv>
#include <osg/TexMat>
#include <osg/Texture2D>
#include <osg/Timer>
#include <osg/MatrixTransform>

#include <osgDB/ReadFile>

#include <osgCal/Model>
#include <osgCal/SubMeshHardware>
#include <osgCal/SubMeshSoftware>

using namespace osgCal;

class CalUpdateCallback: public osg::NodeCallback 
{
    public:

        CalUpdateCallback()
            : previous(0)
            , prevTime(0)
        {}

        virtual void operator()( osg::Node*        node,
                                 osg::NodeVisitor* nv )
        {
            Model* model = dynamic_cast< Model* >( node );

            if ( previous == 0 )
            {
                previous = timer.tick();
            }
            
            double deltaTime = 0;

            if (!nv->getFrameStamp())
            {
                osg::Timer_t current = timer.tick();
                deltaTime = timer.delta_s(previous, current);
                previous = current;
            }
            else
            {
                double time = nv->getFrameStamp()->getSimulationTime();
                deltaTime = time - prevTime;
                prevTime = time;
            }

            //std::cout << "CalUpdateCallback: " << deltaTime << std::endl;
            if ( deltaTime > 0.0 )
            {
                model->update( deltaTime );
            }
            //std::cout << "CalUpdateCallback: ok" << std::endl;

            traverse(node, nv); // may be needed for user nodes
//            node->dirtyBound(); <- is it necessary?
        }

    private:

        osg::Timer timer;
        osg::Timer_t previous;
        double prevTime;

};

/**
 * For precompilation of shaders we use geode with empty
 * drawables but different shaders. It's unnecessary to draw it so we
 * make this CullCallback to cull this geode.
 */
class ShaderGeodeCullCallback: public osg::NodeCallback 
{
    public:

        ShaderGeodeCullCallback()
            : firstFrame( true )
        {}

        virtual void operator()( osg::Node*        node,
                                 osg::NodeVisitor* nv )
        {
            if ( firstFrame )
            {
                firstFrame = false;
                traverse(node, nv); // traverse to compile shaders
                // but is it necessary? GLObjectsVisitor ignores CullCallback
            }
            else
            {
                osg::Geode* g = static_cast< osg::Geode* >( node );

                g->removeDrawables( 0, g->getNumDrawables() );
                // TODO: why its not culled simply by not calling traverse()?
                // TODO: it would be good to remove geode at all
                //
                // The best case is somehow always cull shader geode,
                // but leave it for shader compilation by
                // GLObjectsVisitor
                // (also need to look, is osgViewer calls
                // GLObjectsVisitor, or it only draws all our empty drawables?)
            }
        }

        bool firstFrame;
};

Model::Model()
    : calModel( 0 )
{
//     std::cout << "Model::Model()" << std::endl;
}

Model::Model( const Model&, const osg::CopyOp& )
    : Group() // to eliminate warning
{
    throw std::runtime_error( "Model copying is not supported" );
}

Model::~Model()
{
    setUpdateCallback( 0 );
    delete calModel;
//     std::cout << "Model::~Model()" << std::endl;
//     std::cout << "CoreModel::referenceCount() = "
//               << coreModel->referenceCount() << std::endl;
}

// static
// osg::Drawable*
// linesDrawable( osg::Vec3Array* lines,
//                const osg::Vec3& color )
// {
//     osg::Geometry* g = new osg::Geometry;
//     osg::Vec3Array* vcolor = new osg::Vec3Array;
//     vcolor->push_back( color );
//     g->setVertexArray( lines );
//     g->setColorArray( vcolor );
//     g->setColorBinding( osg::Geometry::BIND_OVERALL );
//     g->addPrimitiveSet( new osg::DrawArrays( GL_LINES, 0, lines->size() ) );
//     g->getOrCreateStateSet()->setMode(GL_LIGHTING,osg::StateAttribute::OVERRIDE|osg::StateAttribute::OFF);;
//     return g;
// }

void
Model::load( CoreModel* cm,
             MeshTyper* meshTyper,
             MeshFilter* meshFilter )
{
    if ( calModel != 0 )
    {
        throw std::runtime_error( "Model already load" );
    }

    coreModel = cm;

    bool hasAnimations = !coreModel->getAnimationNames().empty();

    if ( hasAnimations )
    {
        vertexBuffer = (VertexBuffer*) cm->getVertexBuffer()->clone( osg::CopyOp::DEEP_COPY_ALL );
    }
    else
    {
        vertexBuffer = const_cast< VertexBuffer* >( cm->getVertexBuffer() );
        // no need to clone, since it won't be mutated
    }

    calModel = new CalModel( coreModel->getCalCoreModel() );
    calModel->update( 0 );

    setUpdateCallback( new CalUpdateCallback() );

    vectorBone = &(calModel->getSkeleton()->getVectorBone());

    if ( meshTyper == 0 )
    {
        meshTyper = new AllMeshesHardware();
    }
    else
    {
        meshTyper->ref();
    }

    // -- Setup shader compilation Geode --
    std::map< osg::StateSet*, bool > usedStateSets;
    if ( hasAnimations )
    {
        shaderGeode = new osg::Geode;

        shaderGeode->setCullCallback( new ShaderGeodeCullCallback() );
        // Since we switch static/skinning shaders in SubMesh::update we
        // need some method to compile shaders before they first used
        // (i.e. before actual animation starts).
        // So we use Geode with empty drawables for used state sets,
        // and GLObjectsVisitor automatically compiles all shaders
        // when osgViewer initialized. 
    }

    // we use matrix transforms for rigid meshes, one transform per bone
    std::map< int, osg::MatrixTransform* > rigidTransforms;

    // -- Process meshes --
    osg::ref_ptr< osg::Geode > geode( new osg::Geode );
    
    for ( size_t i = 0; i < coreModel->getMeshes().size(); i++ )
    {
        const CoreModel::Mesh& mesh = coreModel->getMeshes()[i];
        
        if ( meshFilter != NULL && meshFilter->filter( mesh ) == false )
        {
            continue;
        }

        osg::Geometry* g = 0;
        osg::Drawable* depthSubMesh = 0;

        switch ( meshTyper->type( mesh ) )
        {
            case MT_HARDWARE:
            {
                SubMeshHardware* smhw = new SubMeshHardware( this, i, mesh.rigid );

                g = smhw;
                depthSubMesh = smhw->getDepthSubMesh();

                // -- Add shader state sets for compilation --
                if ( hasAnimations )
                {
                    // no need to add state sets for non-animating
                    // models, since their state sets are not changed
                    if ( usedStateSets.find( mesh.staticHardwareStateSet.get() )
                         == usedStateSets.end() )
                    {
                        osg::Drawable* d = new osg::Geometry;
                        d->setStateSet( mesh.staticHardwareStateSet.get() );
                        usedStateSets[ mesh.staticHardwareStateSet.get() ] = true;
                        shaderGeode->addDrawable( d );
                    }

                    if ( usedStateSets.find( mesh.hardwareStateSet.get() )
                         == usedStateSets.end() )
                    {
                        osg::Drawable* d = new osg::Geometry;
                        d->setStateSet( mesh.hardwareStateSet.get() );
                        usedStateSets[ mesh.hardwareStateSet.get() ] = true;
                        shaderGeode->addDrawable( d );
                    }
                }
                break;
            }

            case MT_SOFTWARE:
                if ( normalBuffer.get() == 0 )
                {
                    // create local normals buffer only if necessary.
#ifdef OSG_CAL_BYTE_BUFFERS
                    const NormalBuffer* src = cm->getNormalBuffer();
                    normalBuffer = new SwNormalBuffer( src->size() );

                    for ( size_t i = 0; i < src->size(); i++ )
                    {
                        (*normalBuffer)[i].x() = (*src)[i].x() / 127.0;
                        (*normalBuffer)[i].y() = (*src)[i].y() / 127.0;
                        (*normalBuffer)[i].z() = (*src)[i].z() / 127.0;
                    }
#else
                    normalBuffer = (NormalBuffer*)
                        (hasAnimations
                         ? cm->getNormalBuffer()->clone( osg::CopyOp::DEEP_COPY_ALL )
                         : cm->getNormalBuffer());
#endif
                }
                
                g = new SubMeshSoftware( this, i, mesh.rigid );
                break;

            default:
                throw std::runtime_error( "Model::load - unknown mesh type" );
        }

        g->setName( mesh.name ); // for debug only, TODO: subject to remove

        if ( hasAnimations && mesh.rigid == false )
            // dynamic only when we have animations
            // and mesh is deformable
        {
            g->setDataVariance( osg::Object::DYNAMIC );
            // ^ No drawing during updates. Otherwise there will be a
            // crash in multithreaded osgViewer modes
            // (first reported by Jan Ciger)
        }
        else
        {            
            g->setDataVariance( osg::Object::STATIC );
        }

        if ( depthSubMesh )
        {
            depthSubMesh->setDataVariance( g->getDataVariance() );
        }

        meshes[ mesh.name ] = g;

        if ( mesh.rigid == false // deformable
             || (mesh.rigid && mesh.rigidBoneId == -1) // unrigged
             || hasAnimations == false ) // no animations
        {
            geode->addDrawable( g );
            if ( depthSubMesh )
            {
                geode->addDrawable( depthSubMesh );
            }
        }
        else
        {
            // for rigged animateable rigid meshes we use bone
            // transform w/o per-vertex transformations
            std::map< int, osg::MatrixTransform* >::iterator
                boneMT = rigidTransforms.find( mesh.rigidBoneId );

            osg::MatrixTransform* mt = 0;
                
            if ( boneMT != rigidTransforms.end() )
            {
                // use ready bone matrix transform
                mt = boneMT->second;
            }
            else
            {
                // create new matrix transform for bone
                mt = new osg::MatrixTransform;

                mt->setDataVariance( osg::Object::DYNAMIC );            
                mt->setMatrix( osg::Matrix::identity() );

                addChild( mt );
                rigidTransforms[ mesh.rigidBoneId ] = mt;

                mt->addChild( new osg::Geode );
            }

            static_cast< osg::Geode* >( mt->getChild( 0 ) )->addDrawable( g );
            if ( depthSubMesh )
            {
                static_cast< osg::Geode* >( mt->getChild( 0 ) )->addDrawable( depthSubMesh );
            }
        }
    }

    meshTyper->unref();

    // -- TBN debug --
//     osg::Vec3Array* t = new osg::Vec3Array;
//     osg::Vec3Array* b = new osg::Vec3Array;
//     osg::Vec3Array* n = new osg::Vec3Array;
//     const NormalBuffer& vb = *(coreModel->getVertexBuffer());
//     const NormalBuffer& tb = *(coreModel->getTangentBuffer());
//     const NormalBuffer& bb = *(coreModel->getBinormalBuffer());
//     const NormalBuffer& nb = *(coreModel->getNormalBuffer());
//     const float scale = 1.0f;

//     for ( size_t i = 0; i < vb.size(); i++ )
//     {
//         t->push_back( vb[i] );
//         b->push_back( vb[i] );
//         n->push_back( vb[i] );
//         t->push_back( vb[i] + tb[i]*scale );
//         b->push_back( vb[i] + bb[i]*scale );
//         n->push_back( vb[i] + nb[i]*scale );
//     }

//     geode->addDrawable( linesDrawable( t, osg::Vec3( 1.0, 0.0, 0.0 ) ) );
//     geode->addDrawable( linesDrawable( b, osg::Vec3( 0.0, 1.0, 0.0 ) ) );
//     geode->addDrawable( linesDrawable( n, osg::Vec3( 0.0, 0.0, 1.0 ) ) );

    // -- Add resulting geodes --
    if ( shaderGeode.valid() && shaderGeode->getNumDrawables() > 0 )
    {
        addChild( shaderGeode.get() );
    }

    if ( geode.valid() && geode->getNumDrawables() > 0 )
    {
        addChild( geode.get() );
    }
}

void
Model::update( double deltaTime ) 
{
    CalMixer* calMixer = (CalMixer*)calModel->getAbstractMixer();

    if ( //calMixer->getAnimationVector().size() == <total animations count>
        calMixer->getAnimationActionList().size() != 0 ||
        calMixer->getAnimationCycle().size() != 0 )
    {
        // we update only when we animate something
        calMixer->updateAnimation(deltaTime); 
        calMixer->updateSkeleton();

        // Model::update is 5-10 times slower than updateAnimation + updateSkeleton
        // for idle animations.
    }
    else
    {
        return; // no animations, nothing to update
    }

    updateNode( this );
}

void
Model::updateNode( osg::Node* node ) 
{
    if ( node == shaderGeode.get() )
    {
        return;
    }
    
    // -- Update SubMeshHardware/Software if child is Geode --
    osg::Geode* geode = dynamic_cast< osg::Geode* >( node );
    if ( geode )
    {
        for ( size_t j = 0; j < geode->getNumDrawables(); j++ )
        {
            osg::Drawable* drawable = geode->getDrawable( j );
            
            SubMeshHardware* hardware = dynamic_cast< SubMeshHardware* >( drawable );
            if ( hardware )
            {
                hardware->update();
                continue;
            }
            
            SubMeshSoftware* software = dynamic_cast< SubMeshSoftware* >( drawable );
            if ( software )
            {
                software->update();
                continue;
            }

            //throw std::runtime_error( "unexpected drawable type" );
            // ^ maybe user add his own drawables
        }

        return;
    }

    // -- Set transformation matrix if child is MatrixTransform --
    osg::MatrixTransform* mt = dynamic_cast< osg::MatrixTransform* >( node );
    if ( mt )
    {
        osg::Drawable* drawable =
            static_cast< osg::Geode* >( mt->getChild( 0 ) )->getDrawable( 0 );
        CoreModel::Mesh* m = 0;
        SubMeshHardware* hardware = dynamic_cast< SubMeshHardware* >( drawable );
        if ( hardware )
        {
            m = hardware->getCoreModelMesh();
        }
        else
        {               
            SubMeshSoftware* software = dynamic_cast< SubMeshSoftware* >( drawable );
            if ( software )
            {
                m = software->getCoreModelMesh();
            }
            else
            {
                throw std::runtime_error( "unexpected drawable type" );
                // ^ currently, first mesh is always SubMeshSoftware/Hardware
            }
        }

        if ( m->getBonesCount() > 1 )
        {
            throw std::runtime_error( "more than one bone in rigid mesh" );
        }

        // -- Set bone matrix --
        if ( m->getBonesCount() == 1 )
        {
            mt->setMatrix( getBoneMatrix( m->getBoneId( 0 ) ) );
        }
        else // hardwareModel->getBoneCount() == 0
        {
            // do not touch default no rotation & zero translation
        }

        return;
    }

    // -- Is group? (MatrixTransform is also a Group BTW) --
    osg::Group* group = dynamic_cast< osg::Group* >( node );
    if ( group )
    {
#ifdef _OPENMP
#pragma omp parallel for
#endif // _OPENMP
        for(size_t i = 0; i < group->getNumChildren(); i++)
        {
            updateNode( group->getChild(i) );
        }
        return;
    }
}

void
Model::blendCycle( int id,
                   float weight,
                   float delay )
{
    getCalModel()->getMixer()->blendCycle( id, weight, delay );
}

void
Model::clearCycle( int id,
                   float delay )
{
    getCalModel()->getMixer()->clearCycle( id, delay );
}

osg::Geometry*
Model::getMesh( const std::string& name ) const throw (std::runtime_error)
{
    MeshMap::const_iterator i = meshes.find( name );

    if ( i != meshes.end() )
    {
        return i->second.get();
    }
    else
    {
        throw std::runtime_error( "Model::getMesh - can't find mesh \"" + name + "\"" );
    }
}


osg::Quat
Model::getBoneRotation( int boneId ) const
{
    CalQuaternion r = (*vectorBone)[ boneId ]->getRotationBoneSpace();
    return osg::Quat( r.x, r.y, r.z, r.w );
}

osg::Vec3
Model::getBoneTranslation( int boneId ) const
{
    CalVector t =  (*vectorBone)[ boneId ]->getTranslationBoneSpace();
    return osg::Vec3( t.x, t.y, t.z );
}

void
Model::getBoneRotationTranslation( int boneId,
                                   GLfloat* rotation,
                                   GLfloat* translation ) const
{
    CalQuaternion   rotationBoneSpace = (*vectorBone)[ boneId ]->getRotationBoneSpace();
    CalVector       translationBoneSpace =  (*vectorBone)[ boneId ]->getTranslationBoneSpace();

    CalMatrix       rotationMatrix = rotationBoneSpace;

    rotation[0] = rotationMatrix.dxdx;
    rotation[1] = rotationMatrix.dxdy;
    rotation[2] = rotationMatrix.dxdz;
    rotation[3] = rotationMatrix.dydx;
    rotation[4] = rotationMatrix.dydy;
    rotation[5] = rotationMatrix.dydz;
    rotation[6] = rotationMatrix.dzdx;
    rotation[7] = rotationMatrix.dzdy;
    rotation[8] = rotationMatrix.dzdz;

    translation[0] = translationBoneSpace.x;
    translation[1] = translationBoneSpace.y;
    translation[2] = translationBoneSpace.z;
}

osg::Matrixf
Model::getBoneMatrix( int boneId ) const
{
    CalQuaternion   rotation = (*vectorBone)[ boneId ]->getRotationBoneSpace();
    CalVector       translation =  (*vectorBone)[ boneId ]->getTranslationBoneSpace();

    CalMatrix       rm = rotation;

    return osg::Matrixf( rm.dxdx   , rm.dydx   , rm.dzdx   , 0.0f,
                         rm.dxdy   , rm.dydy   , rm.dzdy   , 0.0f,
                         rm.dxdz   , rm.dydz   , rm.dzdz   , 0.0f,
                         translation.x, translation.y, translation.z, 1.0f );
}


std::pair< osg::Matrix3, osg::Vec3 >
Model::getBoneRotationTranslation( int boneId ) const
{
    CalQuaternion   rotation = (*vectorBone)[ boneId ]->getRotationBoneSpace();
    CalVector       translation =  (*vectorBone)[ boneId ]->getTranslationBoneSpace();

    CalMatrix       rm = rotation;

    return std::make_pair(
        osg::Matrix3( rm.dxdx   , rm.dydx   , rm.dzdx   ,
                      rm.dxdy   , rm.dydy   , rm.dzdy   ,
                      rm.dxdz   , rm.dydz   , rm.dzdz   ),
        osg::Vec3( translation.x, translation.y, translation.z ) );
}
