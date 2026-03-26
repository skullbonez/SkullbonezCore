/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/



/*-----------------------------------------------------------------------------------
								  THE SKULLBONEZ CORE
										_______
								     .-"       "-.
									/             \
								   /               \
								   ?   .--. .--.   ?
								   ? )/   ? ?   \( ?
								   ?/ \__/   \__/ \?
								   /      /^\      \
								   \__    '='    __/
								   	 ?\         /?
									 ?\'"VUUUV"'/?
									 \ `"""""""` /
									  `-._____.-'

								 www.simoneschbach.com
-----------------------------------------------------------------------------------*/



/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezSpatialGrid.h"



/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Math::CollisionDetection;



/* -- HASH CELL -------------------------------------------------------------------*/
int64_t SpatialGrid::HashCell(int ix, int iy, int iz)
{
	return (int64_t(ix) * 73856093) ^ (int64_t(iy) * 19349663) ^ (int64_t(iz) * 83492791);
}



/* -- CONSTRUCTOR -----------------------------------------------------------------*/
SpatialGrid::SpatialGrid(float fCellSize)
	: cellSize(fCellSize), inverseCellSize(1.0f / fCellSize) {}



/* -- CLEAR -----------------------------------------------------------------------*/
void SpatialGrid::Clear(void)
{
	for(auto& pair : cells)
		pair.second.clear();
	cells.clear();
}



/* -- INSERT ----------------------------------------------------------------------*/
void SpatialGrid::Insert(int index, const Vector3& position, float radius)
{
	int minX = (int)floorf((position.x - radius) * inverseCellSize);
	int minY = (int)floorf((position.y - radius) * inverseCellSize);
	int minZ = (int)floorf((position.z - radius) * inverseCellSize);
	int maxX = (int)floorf((position.x + radius) * inverseCellSize);
	int maxY = (int)floorf((position.y + radius) * inverseCellSize);
	int maxZ = (int)floorf((position.z + radius) * inverseCellSize);

	for(int ix = minX; ix <= maxX; ++ix)
		for(int iy = minY; iy <= maxY; ++iy)
			for(int iz = minZ; iz <= maxZ; ++iz)
				cells[HashCell(ix, iy, iz)].push_back(index);
}



/* -- GET CANDIDATE PAIRS ---------------------------------------------------------*/
void SpatialGrid::GetCandidatePairs(std::vector<std::pair<int,int>>& outPairs)
{
	outPairs.clear();
	std::unordered_set<int64_t> seen;

	for(const auto& cell : cells)
	{
		const std::vector<int>& indices = cell.second;
		for(int i = 0; i < (int)indices.size() - 1; ++i)
		{
			for(int j = i + 1; j < (int)indices.size(); ++j)
			{
				int a = (std::min)(indices[i], indices[j]);
				int b = (std::max)(indices[i], indices[j]);
				int64_t key = (int64_t(a) << 32) | int64_t(b);

				if(seen.insert(key).second)
					outPairs.push_back(std::make_pair(a, b));
			}
		}
	}
}
