/* -*- c++ -*-
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
#include <osg/VertexProgram>
#include <osg/GL2Extensions>
#include <osg/CullFace>

#include <osgCal/SubMeshHardware>

#include <GL/glu.h> // Ovidiu Sabou (for compiling using Visual C++):
                    // The glu header must be included after the osg
                    // headers (which in turn include windows.h
                    // first).

using namespace osgCal;



SubMeshHardware::SubMeshHardware( Model*     _model,
                                  int        meshIndex,
                                  bool       _meshIsStatic )
    : coreModel( _model->getCoreModel() )
    , model( _model )
    , calModel( _model->getCalModel() )
    , mesh( const_cast< CoreModel::Mesh* >( &_model->getCoreModel()->getMeshes()[ meshIndex ] ) )
    , meshIsStatic( _meshIsStatic )
{   
    if ( mesh->maxBonesInfluence == 0 || meshIsStatic )
    {
        setUseDisplayList( false/*true*/ );
        setSupportsDisplayList( false/*true*/ ); // won't work otherwise ???
        // faster (when true) but there are some errors (like not
        // showing some meshes #84). Plus some meshes are erroneously
        // modulated with material colors from other meshes
        // (why???, bug in OSG?). Plus incorrect display of
        // transparent meshes (only single first pass remembered).
        // So currently we do not use display lists for static
        // hardware meshes (but it works for software ones).
        setStateSet( mesh->staticHardwareStateSet.get() );
    }
    else
    {
        setUseDisplayList( false );
        setSupportsDisplayList( false );
        // when set to true and animating huge fps falloff due to
        // continuous display list rebuilding
        setStateSet( mesh->hardwareStateSet.get() );
    }
    setUseVertexBufferObjects( false ); // false is default

    create();

// its too expensive to create different stateset for each submesh
// which (stateset) differs only in two uniforms
// its better to set them in drawImplementation
//     setStateSet( new osg::StateSet( *mesh->hardwareStateSet.get(), osg::CopyOp::SHALLOW_COPY ) );
//     getStateSet()->addUniform( new osg::Uniform( osg::Uniform::FLOAT_VEC3, "translationVectors",
//                                                  30 /*OSGCAL_MAX_BONES_PER_MESH*/ ) );
//     getStateSet()->addUniform( new osg::Uniform( osg::Uniform::FLOAT_MAT3, "rotationMatrices",
//                                                  30 /*OSGCAL_MAX_BONES_PER_MESH*/ ) );
}

osg::Object*
SubMeshHardware::cloneType() const
{
    throw std::runtime_error( "cloneType() is not implemented" );
}

osg::Object*
SubMeshHardware::clone( const osg::CopyOp& ) const
{
    throw std::runtime_error( "clone() is not implemented" );
}

void
SubMeshHardware::drawImplementation(osg::RenderInfo& renderInfo) const
{
    //std::cout << "SubMeshHardware::drawImplementation: start" << std::endl;
    osg::State& state = *renderInfo.getState();
    
    CalHardwareModel* hardwareModel = coreModel->getCalHardwareModel();
    
    hardwareModel->selectHardwareMesh( mesh->hardwareMeshId );

    // -- Bind our vertex buffers --
    state.disableAllVertexArrays();
    
    const osg::Program::PerContextProgram* program =
        static_cast< const osg::Program* >(
            getStateSet()->
            getAttribute( osg::StateAttribute::PROGRAM ) )->
            getPCP( state.getContextID() );
        //mesh->animatingShadergetPCP( state.getContextID() );
        //coreModel->getSkeletalProgram()->getPCP( state.getContextID() );

#define BIND(_type)                                                     \
    coreModel->getVbo(CoreModel::BI_##_type)->compileBuffer( state );   \
    coreModel->getVbo(CoreModel::BI_##_type)->bindBuffer( state.getContextID() )

#define UNBIND(_type)                                                   \
    coreModel->getVbo(CoreModel::BI_##_type)->unbindBuffer( state.getContextID() )

    if ( program->getAttribLocation( "weight" ) > 0 )
    {
        BIND( WEIGHT );
        state.setVertexAttribPointer( program->getAttribLocation( "weight" ),
                                      4 , GL_FLOAT, false, 0,0);
    }

    if ( program->getAttribLocation( "normal" ) > 0 )
    {
        BIND( NORMAL );
        state.setVertexAttribPointer( program->getAttribLocation( "normal" ),
                                      3 , GL_FLOAT, false, 0,0);
    }

    if ( program->getAttribLocation( "index" ) > 0 )
    {
        BIND( MATRIX_INDEX );
        state.setVertexAttribPointer( program->getAttribLocation( "index" ),
//                                  4 , GL_INT, false, 0,0);   // dvorets - 17.5fps
//                                  4 , GL_BYTE, false, 0,0);  // dvorets - 20fps
//                                  4 , GL_FLOAT, false, 0,0); // dvorets - 53fps
                                      4 , GL_SHORT, false, 0,0); // dvorets - 57fps - GLSL int = 16 bit
    }

    if ( program->getAttribLocation( "binormal" ) > 0 )
    {
        BIND( BINORMAL );
        state.setVertexAttribPointer( program->getAttribLocation( "binormal" ),
                                      3 , GL_FLOAT, false, 0,0);
    }

    if ( program->getAttribLocation( "tangent" ) > 0 )
    {
        BIND( TANGENT );
        state.setVertexAttribPointer( program->getAttribLocation( "tangent" ),
                                      3 , GL_FLOAT, false, 0,0);
    }

    if ( program->getAttribLocation( "texCoord" ) > 0 )
    {
        BIND( TEX_COORD );
        state.setVertexAttribPointer( program->getAttribLocation( "texCoord" ),
                                      2 , GL_FLOAT, false, 0,0);
    }

    BIND( VERTEX );
    state.setVertexAttribPointer( program->getAttribLocation( "position" ),
                                  3 , GL_FLOAT, false, 0,0);

    // -- Calculate and bind rotation/translation uniforms --
    const osg::GL2Extensions* gl2extensions = osg::GL2Extensions::Get( state.getContextID(), true );

    GLint rotationMatricesAttrib = program->getUniformLocation( "rotationMatrices" );
    if ( rotationMatricesAttrib < 0 )
    {
        rotationMatricesAttrib = program->getUniformLocation( "rotationMatrices[0]" );
        // Why the hell on ATI it has uniforms for each
        // elements? (nVidia has only one uniform for whole array)
    }
    
    GLint translationVectorsAttrib = program->getUniformLocation( "translationVectors" );
    if ( translationVectorsAttrib < 0 )
    {
        translationVectorsAttrib = program->getUniformLocation( "translationVectors[0]" );
    }

    if ( rotationMatricesAttrib < 0 || translationVectorsAttrib < 0 )
    {
        ; // in static shader we can get no rotation/translation attributes
    }
    else
    {
        // otherwise setup uniforms
        for( int boneId = 0; boneId < hardwareModel->getBoneCount(); boneId++ )
        {
            CalQuaternion   rotationBoneSpace =
                hardwareModel->getRotationBoneSpace( boneId, calModel->getSkeleton() );
            CalVector       translationBoneSpace =
                hardwareModel->getTranslationBoneSpace( boneId, calModel->getSkeleton() );

            CalMatrix       rotationMatrix = rotationBoneSpace;
            GLfloat         rotation[9];
            GLfloat         translation[3];

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

            gl2extensions->glUniformMatrix3fv( rotationMatricesAttrib + boneId,
                                               1, GL_TRUE, rotation );
            gl2extensions->glUniform3fv( translationVectorsAttrib + boneId,
                                         1, translation );
        }
    
        GLfloat rotation[9] = {1,0,0, 0,1,0, 0,0,1};
        GLfloat translation[3] = {0,0,0};
        gl2extensions->glUniformMatrix3fv( rotationMatricesAttrib + 30, 1, GL_TRUE, rotation );
        gl2extensions->glUniform3fv( translationVectorsAttrib + 30, 1, translation );
    }

    // -- Brute force fix of display list bug --
    // when using display lists and we have two (or more) meshes with
    // the same state sets (hand, legs) we get second mesh modulated
    // with some other mesh diffuse (ambient?) color (red, green,
    // black, gray). So we manually apply material state here
//     const osg::Material* material = static_cast< const osg::Material* >
//         ( getStateSet()->getAttribute( osg::StateAttribute::MATERIAL ) );

//     material->apply( state );
    // ^ it is also bad, since it kills any material overriding

    // -- Draw our indexed triangles --
    BIND( INDEX ); 
    
#define DRAW                                                            \
    if ( sizeof(CalIndex) == 2 )                                        \
        glDrawElements( GL_TRIANGLES,                                   \
                        mesh->getIndexesCount(),                        \
                        GL_UNSIGNED_SHORT,                              \
                        ((CalIndex *)NULL) + mesh->getIndexInVbo() );   \
    else                                                                \
        glDrawElements( GL_TRIANGLES,                                   \
                        mesh->getIndexesCount(),                        \
                        GL_UNSIGNED_INT,                                \
                        ((CalIndex *)NULL) + mesh->getIndexInVbo() );

#define glError()                                                       \
    {                                                                   \
        GLenum err = glGetError();                                      \
        while (err != GL_NO_ERROR) {                                    \
            fprintf(stderr, "glError: %s caught at %s:%u\n",            \
                    (char *)gluErrorString(err), __FILE__, __LINE__);   \
            err = glGetError();                                         \
        }                                                               \
    }

    GLint faceUniform = program->getUniformLocation( "face" );

    if ( mesh->staticHardwareStateSet.get()->getRenderingHint()
         & osg::StateSet::TRANSPARENT_BIN )
    {
        glCullFace( GL_FRONT ); // first draw only back faces
        gl2extensions->glUniform1f( faceUniform, -1.0f );
        DRAW;
        gl2extensions->glUniform1f( faceUniform, 1.0f );
        glCullFace( GL_BACK ); // then draw only front faces
        DRAW;
    }
    else
    {
        if ( mesh->hwStateDesc.sides == 2 ) 
        {
            //glCullFace( GL_BACK ); // (already enabled) draw only front faces
            gl2extensions->glUniform1f( faceUniform, 1.0f );
            DRAW;
            glCullFace( GL_FRONT ); // then draw only back faces
            gl2extensions->glUniform1f( faceUniform, -1.0f );
            DRAW;
            glCullFace( GL_BACK ); // restore state
        }
        else // single sided (or undefined) -- simply draw it
        {
            gl2extensions->glUniform1f( faceUniform, 1.0f );
            DRAW; // simple draw for single-sided non-transparent meshes
        }
    }
    
    //glError();

    state.disableVertexAttribPointer(program->getAttribLocation( "position" ));
    if ( program->getAttribLocation( "weight" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "weight" ));
    if ( program->getAttribLocation( "normal" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "normal" ));
    if ( program->getAttribLocation( "index" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "index" ));
    if ( program->getAttribLocation( "texCoord" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "texCoord" ));
    if ( program->getAttribLocation( "binormal" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "binormal" ));
    if ( program->getAttribLocation( "tangent" ) > 0 )
        state.disableVertexAttribPointer(program->getAttribLocation( "tangent" ));

    UNBIND( TEX_COORD ); // extensions->glBindBuffer(GL_ARRAY_BUFFER_ARB,0);
    UNBIND( INDEX ); // extensions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB,0);

    //std::cout << "SubMeshHardware::drawImplementation: end" << std::endl;
}

void
SubMeshHardware::create()
{
    setVertexArray( model->getVertexBuffer() );

    osg::DrawElementsUInt* de = new osg::DrawElementsUInt( osg::PrimitiveSet::TRIANGLES,
                                                           mesh->getIndexesCount() );
    memcpy( &de->front(),
            (GLuint*)&coreModel->getIndexBuffer()->front()
            + mesh->getIndexInVbo(),
            mesh->getIndexesCount() * sizeof ( GLuint ) );
    addPrimitiveSet( de );

// draw arrays works slower for picking than draw elements.
//     setVertexIndices( const_cast< IndexBuffer* >( coreModel->getIndexBuffer() ) );
//     addPrimitiveSet( new osg::DrawArrays( osg::PrimitiveSet::TRIANGLES,
//                                           mesh->getIndexInVbo(),
//                                           mesh->getIndexesCount() ) );

    boundingBox = mesh->boundingBox;
}

static
inline
osg::Vec3f
mul3( const osg::Matrix3& m,
      const osg::Vec3f& v )
{
    return osg::Vec3f( m(0,0)*v.x() + m(1,0)*v.y() + m(2,0)*v.z(),
                       m(0,1)*v.x() + m(1,1)*v.y() + m(2,1)*v.z(),
                       m(0,2)*v.x() + m(1,2)*v.y() + m(2,2)*v.z() );
}
namespace osg {
bool
operator == ( const osg::Matrix3& m1,
              const osg::Matrix3& m2 )
{
    for ( int i = 0; i < 9; i++ )
    {
        if ( m1.ptr()[ i ] != m2.ptr()[ i ] )
        {
            return false; 
        }
    }
    
    return true;
}
}

void
SubMeshHardware::update()
{   
    if ( mesh->maxBonesInfluence == 0 || meshIsStatic )
    {
        return; // no bones - no update
    }
    
    // -- Setup rotation matrices & translation vertices --
    CalHardwareModel* hardwareModel = coreModel->getCalHardwareModel();

    // TODO: it is not thread safe!!!
    // one hardware model for each model & one vertex buffer for all sub-meshes
    // we can make separate vertex buffers, but do we really need them.
    // we have meshes without common points (but also we have models with big amount
    // of small meshes)
    //
    // i think per-model update lock would be enough
    std::vector< std::pair< osg::Matrix3, osg::Vec3f > > rotationTranslationMatrices;

    bool deformed = false;

#ifdef _OPENMP
    #pragma omp critical
#endif // _OPENMP
    {
        hardwareModel->selectHardwareMesh( mesh->hardwareMeshId );

        for( int boneId = 0; boneId < hardwareModel->getBoneCount(); boneId++ )
        {
            CalQuaternion   rotationBoneSpace =
                hardwareModel->getRotationBoneSpace( boneId, calModel->getSkeleton() );
            CalVector       translationBoneSpace =
                hardwareModel->getTranslationBoneSpace( boneId, calModel->getSkeleton() );

            CalMatrix       rotationMatrix = rotationBoneSpace;
            GLfloat         rotation[9];
            GLfloat         translation[3];

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

            osg::Matrix3 r( rotation[0], rotation[3], rotation[6],
                            rotation[1], rotation[4], rotation[7], 
                            rotation[2], rotation[5], rotation[8] );
//             osg::Matrixf r( rotation[0], rotation[3], rotation[6], 0,
//                             rotation[1], rotation[4], rotation[7], 0, 
//                             rotation[2], rotation[5], rotation[8], 0,
//                             0          , 0          , 0          , 1 );
            osg::Vec3 v( translation[0],
                         translation[1],
                         translation[2] );
        
            rotationTranslationMatrices.push_back( std::make_pair( r, v ) );

            if ( rotation[0] != 1 || rotation[4] != 1 || rotation[8] != 1 ||
                 translation[0] != 0 || translation[1] != 0 || translation[2] != 0 )
            {
                deformed = true;
            }
        }
    }    

    rotationTranslationMatrices.resize( 31 );
    rotationTranslationMatrices[ 30 ] = // last always identity (see #68)
        std::make_pair( osg::Matrix3( 1, 0, 0,
                                      0, 1, 0,
                                      0, 0, 1 ),
                        osg::Vec3( 0, 0, 0 ) );

    // -- Check for deformation state and select state set type --
    if ( deformed )
    {
        setStateSet( mesh->hardwareStateSet.get() );
    }
    else
    {
        setStateSet( mesh->staticHardwareStateSet.get() );
        // for undeformed meshes we use static state set which not
        // perform vertex, normal, binormal and tangent deformations
        // in vertex shader
    }

    // -- Check changes --
    if ( rotationTranslationMatrices == previousRotationTranslationMatrices )
    {
        return; // no changes
    }
    else
    {
        previousRotationTranslationMatrices = rotationTranslationMatrices;
    }

    // -- Scan indexes --
    boundingBox = osg::BoundingBox();
    
    const GLuint* beginIndex = &coreModel->getIndexBuffer()->front() + mesh->getIndexInVbo();
    const GLuint* endIndex   = beginIndex + mesh->getIndexesCount();

    VertexBuffer&               vb  = *model->getVertexBuffer();
    Model::UpdateFlagBuffer&    ufb = *model->getUpdateFlagBuffer();
    const VertexBuffer&         svb = *coreModel->getVertexBuffer();
    const WeightBuffer&         wb  = *coreModel->getWeightBuffer();
    const MatrixIndexBuffer&    mib = *coreModel->getMatrixIndexBuffer();    
   
#define ITERATE( _f )                                               \
    for ( const GLuint* idx = beginIndex; idx < endIndex; ++idx )	\
    {                                                               \
        if ( ufb[ *idx ] )                                          \
        {                                                           \
            continue; /* skip already processed vertices */         \
        }                                                           \
        else                                                        \
        {                                                           \
            ufb[ *idx ] = GL_TRUE;                                  \
        }                                                           \
                                                                    \
        osg::Vec3f&        v  = vb [ *idx ];                        \
        const osg::Vec3f&  sv = svb[ *idx ]; /* source vector */	\
        const osg::Vec4f&  w  = wb [ *idx ]; /* weights */          \
        const osg::Vec4s&  mi = mib[ *idx ]; /* bone indexes */		\
                                                                    \
        _f;                                                         \
                                                                    \
        boundingBox.expandBy( v );                                  \
    }

#define PROCESS_X( _process_y )                                                 \
    if ( w.x() )                                                                \
    {                                                                           \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi.x()].first;     \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi.x()].second;    \
        v = (mul3(rm, sv) + tv) * w.x();                                        \
                                                                                \
        _process_y;                                                             \
    }                                                                           \
    else                                                                        \
    {                                                                           \
        v = sv;                                                                 \
    }

#define PROCESS_Y( _process_z )                                                 \
    if ( w.y() )                                                                \
    {                                                                           \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi.y()].first;     \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi.y()].second;    \
        v += (mul3(rm, sv) + tv) * w.y();                                       \
                                                                                \
        _process_z;                                                             \
    }                                                                           \

#define PROCESS_Z( _process_w )                                                 \
    if ( w.z() )                                                                \
    {                                                                           \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi.z()].first;     \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi.z()].second;    \
        v += (mul3(rm, sv) + tv) * w.z();                                       \
                                                                                \
        _process_w;                                                             \
    }                                                                           \

#define PROCESS_W()                                                             \
    if ( w.w() )                                                                \
    {                                                                           \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi.w()].first;     \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi.w()].second;    \
        v += (mul3(rm, sv) + tv) * w.w();                                       \
    }                                                                           \

#define STOP

    switch ( mesh->maxBonesInfluence )
    {
        case 1:
	  //#pragma omp parallel for -
	  // w/o      - 1.5s
	  // 1thread  - 1.7s
	  // 2threads - 2.1s => no win here
            ITERATE( PROCESS_X( STOP ) );
            break;

        case 2:
	  //#pragma omp parallel for						
            ITERATE( PROCESS_X( PROCESS_Y ( STOP ) ) );
            break;

        case 3:
	  //#pragma omp parallel for					       
            ITERATE( PROCESS_X( PROCESS_Y ( PROCESS_Z( STOP ) ) ) );
            break;

        case 4:
	  //#pragma omp parallel for						
            ITERATE( PROCESS_X( PROCESS_Y ( PROCESS_Z( PROCESS_W() ) ) ) );
            break;

        default:
            throw std::runtime_error( "maxBonesInfluence > 4 ???" );            
    }

    dirtyBound();

//    dirtyDisplayList(); //<- no display list for deformable mesh?
    // TODO: investigate display list stuff 
}
