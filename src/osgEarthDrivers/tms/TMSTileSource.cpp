/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2013 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include "TMSTileSource"
#include <osgEarth/ImageUtils>

using namespace osgEarth;
using namespace osgEarth::Util;
using namespace osgEarth::Drivers::TileMapService;

#define LC "[TMSTileSource] "


TMSTileSource::TMSTileSource(const TileSourceOptions& options) :
TileSource(options),
_options  (options)
{
    _invertY = _options.tmsType() == "google";
}


TileSource::Status
TMSTileSource::initialize(const osgDB::Options* dbOptions)
{
    _dbOptions = Registry::instance()->cloneOrCreateOptions(dbOptions);

    const Profile* profile = getProfile();

    URI tmsURI = _options.url().value();
    if ( tmsURI.empty() )
    {
        return Status::Error( "Fail: TMS driver requires a valid \"url\" property" );
    }

    // Take the override profile if one is given
    if ( profile )
    {
        OE_INFO << LC 
            << "Using override profile \"" << getProfile()->toString() 
            << "\" for URI \"" << tmsURI.base() << "\"" 
            << std::endl;

        _tileMap = TMS::TileMap::create( 
            _options.url()->full(),
            profile,
            _options.format().value(),
            _options.tileSize().value(), 
            _options.tileSize().value() );
    }

    else
    {
        // Attempt to read the tile map parameters from a TMS TileMap XML tile on the server:
        _tileMap = TMS::TileMapReaderWriter::read( tmsURI.full(), _dbOptions.get() );
        if (!_tileMap.valid())
        {
            return Status::Error( Stringify() << "Failed to read tilemap from " << tmsURI.full() );
        }

        OE_INFO << LC
            << "TMS tile map datestamp = "
            << DateTime(_tileMap->getTimeStamp()).asRFC1123()
            << std::endl;

        profile = _tileMap->createProfile();
        if ( profile )
            setProfile( profile );
    }

    // Make sure we've established a profile by this point:
    if ( !profile )
    {
        return Status::Error( Stringify() << "Failed to establish a profile for " << tmsURI.full() );
    }

    // TileMap and profile are valid at this point. Build the tile sets.
    // Automatically set the min and max level of the TileMap
    if ( _tileMap->getTileSets().size() > 0 )
    {
        OE_DEBUG << LC << "TileMap min/max " << _tileMap->getMinLevel() << ", " << _tileMap->getMaxLevel() << std::endl;
        if (_tileMap->getDataExtents().size() > 0)
        {
            for (DataExtentList::iterator itr = _tileMap->getDataExtents().begin(); itr != _tileMap->getDataExtents().end(); ++itr)
            {
                this->getDataExtents().push_back(*itr);
            }
        }
        else
        {
            //Push back a single area that encompasses the whole profile going up to the max level
            this->getDataExtents().push_back(DataExtent(profile->getExtent(), 0, _tileMap->getMaxLevel()));
        }
    }
    return STATUS_OK;
}


TimeStamp
TMSTileSource::getLastModifiedTime() const
{
    if ( _tileMap.valid() )
        return _tileMap->getTimeStamp();
    else
        return TileSource::getLastModifiedTime();
}


CachePolicy
TMSTileSource::getCachePolicyHint(const Profile* targetProfile) const
{
    // if the source is local and the profiles line up, don't cache (by default).
    if (!_options.url()->isRemote() &&
        targetProfile && 
        targetProfile->isEquivalentTo(getProfile()) )
    {
        return CachePolicy::NO_CACHE;
    }
    else
    {
        return CachePolicy::DEFAULT;
    }
}


osg::Image*
TMSTileSource::createImage(const TileKey&    key,
                           ProgressCallback* progress)
{
    if (_tileMap.valid() && key.getLevelOfDetail() <= _tileMap->getMaxLevel() )
    {
        std::string image_url = _tileMap->getURL( key, _invertY );

        osg::ref_ptr<osg::Image> image;
        if (!image_url.empty())
        {
            image = URI(image_url).readImage( _dbOptions.get(), progress ).getImage();
        }

        if (!image.valid())
        {
            if (image_url.empty() || !_tileMap->intersectsKey(key))
            {
                //We couldn't read the image from the URL or the cache, so check to see if the given key is less than the max level
                //of the tilemap and create a transparent image.
                if (key.getLevelOfDetail() <= _tileMap->getMaxLevel())
                {
                    OE_DEBUG << LC << "Returning empty image " << std::endl;
                    return ImageUtils::createEmptyImage();
                }
            }
        }
        return image.release();
    }
    return 0;
}


int
TMSTileSource::getPixelsPerTile() const
{
    return _tileMap->getFormat().getWidth();
}

std::string
TMSTileSource::getExtension() const 
{
    return _tileMap->getFormat().getExtension();
}