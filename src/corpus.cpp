#include "corpus.hpp"

void corpus::loadData(const char* voteFile, const char* imgFeatPath, int userMin, int itemMin)
{
	nItems = 0;
	nUsers = 0;
	nVotes = 0;

  imFeatureDim = 4096;

	/// Note that order matters here
	loadVotes(imgFeatPath, voteFile, userMin, itemMin);	
  loadImgFeatures(imgFeatPath);
	
	fprintf(stderr, "\n  \"nUsers\": %d, \"nItems\": %d, \"nVotes\": %d\n", nUsers, nItems, nVotes);
}

void corpus::cleanUp()
{
	for (vector<vote*>::iterator it = V.begin(); it != V.end(); it++) {
		delete *it;
	}
}

void corpus::loadVotes(const char* imgFeatPath, const char* voteFile, int userMin, int itemMin)
{

  //image part
  FILE* f = fopen_(imgFeatPath, "rb");
  fprintf(stderr, "\n  Pre-loading image asins from %s  ", imgFeatPath);

  float* feat = new float [imFeatureDim];
  char* asin = new char [11];
  asin[10] = '\0';
  int a;
  int counter = 0;
  while (!feof(f)) {
    
    if ((a = fread(asin, sizeof(*asin), 10, f)) != 10) { // last line might be empty
      continue;
    }
    
    
    
    // trim right space
    string sAsin(asin);
    size_t found = sAsin.find(" ");
    if (found != string::npos) {
        sAsin = sAsin.substr(0, found);
    }

    for (unsigned c = 0; c < sAsin.size(); c ++) {
      if (not isascii(asin[c])) {
        printf("Expected asin to be 10-digit ascii\n");
        exit(1);
      }
    }
    if (not (counter % 10000)) {
      fprintf(stderr, ".");
      fflush(stderr);
    }

    if ((a = fread(feat, sizeof(*feat), imFeatureDim, f)) != imFeatureDim) {
      printf("Expected to read %d floats, got %d\n", imFeatureDim, a);
      exit(1);
    }
    imgAsins[sAsin] = 1;
    counter ++;
  }
  fprintf(stderr, "\n");

  delete[] asin;
  delete [] feat;
  fclose(f);
  
  //end image part

	fprintf(stderr, "  Loading votes from %s, userMin = %d, itemMin = %d  ", voteFile, userMin, itemMin);

	string uName; // User name
	string bName; // Item name
	float value; // Rating
	long long voteTime; // Time rating was entered
	map<pair<int, int>, long long> voteMap;

	int nRead = 0; // Progress
	string line;

	igzstream in;
	in.open(voteFile);
	if (! in.good()) {
		fprintf(stderr, "Can't read votes from %s.\n", voteFile);
		exit(1);
	}

	// The first pass is for filtering
	while (getline(in, line)) {
		stringstream ss(line);
		ss >> uName >> bName >> value;

		nRead++;
		if (nRead % 100000 == 0) {
			fprintf(stderr, ".");
			fflush(stderr);
		}

    if (imgAsins.find(bName) == imgAsins.end()) {
      continue;
    }

		if (value > 5 or value < 0) { // Ratings should be in the range [0,5]
			printf("Got bad value of %f\nOther fields were %s %s %lld\n", value, uName.c_str(), bName.c_str(), voteTime);
			exit(1);
		}

		if (uCounts.find(uName) == uCounts.end()) {
			uCounts[uName] = 0;
		}
		if (bCounts.find(bName) == bCounts.end()) {
			bCounts[bName] = 0;
		}
		uCounts[uName]++;
		bCounts[bName]++;
	}
	in.close();

	// Re-read
	nUsers = 0;
	nItems = 0;
	
	igzstream in2;
	in2.open(voteFile);
	if (! in2.good()) {
		fprintf(stderr, "Can't read votes from %s.\n", voteFile);
		exit(1);
	}

	vector<vector<double> > ratingPerItem;

	nRead = 0;
	while (getline(in2, line)) {
		stringstream ss(line);
		ss >> uName >> bName >> value >> voteTime;
    // reviewer_id, asin, overall, time, text

		nRead++;
		if (nRead % 100000 == 0) {
			fprintf(stderr, ".");
			fflush(stderr);
		}

    if (imgAsins.find(bName) == imgAsins.end()) {
      continue;
    }

    if (uCounts[uName] < userMin or bCounts[bName] < itemMin) {
      continue;
    }

		// new item
		if (itemIds.find(bName) == itemIds.end()) {
			rItemIds[nItems] = bName;
			itemIds[bName] = nItems++;
			vector<double> vec;
			ratingPerItem.push_back(vec);
		}
		// new user
		if (userIds.find(uName) == userIds.end()) {
			rUserIds[nUsers] = uName;
			userIds[uName] = nUsers++;
		}

		ratingPerItem[itemIds[bName]].push_back(value);

    // this is a diction of u,i pairs w/ a value of time
    //(u,i)=>time
		voteMap[make_pair(userIds[uName], itemIds[bName])] = voteTime;	
	}
	in2.close();

	for (int x = 0; x < nItems; x ++) {
		numReviewsPerItem.push_back(ratingPerItem[x].size());
		double sum = 0;
		for (int j = 0; j < (int)ratingPerItem[x].size(); j ++) {
			sum += ratingPerItem[x].at(j);
		}
		if (ratingPerItem[x].size() > 0) {
			avgRatingPerItem.push_back(sum / ratingPerItem[x].size());
		} else {
			avgRatingPerItem.push_back(0);
		}
	}

	fprintf(stderr, "\n");
	generateVotes(voteMap);
}

void corpus::generateVotes(map<pair<int, int>, long long>& voteMap)
{
	fprintf(stderr, "\n  Generating votes data ");
	
	for(map<pair<int, int>, long long>::iterator it = voteMap.begin(); it != voteMap.end(); it ++) {
		vote* v = new vote();
		v->user = it->first.first;
		v->item = it->first.second;
		v->voteTime = it->second;
		v->label = 1; // positive
		V.push_back(v);
	}
	
	nVotes = V.size();
	random_shuffle(V.begin(), V.end());
}

void corpus::loadImgFeatures(const char* imgFeatPath)
{
  for (int i = 0; i < nItems; i ++) {
    vector<pair<int, float> > vec;
    imageFeatures.push_back(vec);
  }

  FILE* f = fopen_(imgFeatPath, "rb");
  fprintf(stderr, "\n  Loading image features from %s  ", imgFeatPath);

  float ma = 58.388599; // Largest feature observed

  float* feat = new float [imFeatureDim];
  char* asin = new char [11];
  asin[10] = '\0';
  int a;
  int counter = 0;
  while (!feof(f)) {
    if ((a = fread(asin, sizeof(*asin), 10, f)) != 10) {
      //printf("Expected to read %d chars, got %d\n", 10, a);
      continue;
    }
        // trim right space
        string sAsin(asin);
        size_t found = sAsin.find(" ");
        if (found != string::npos) {
            sAsin = sAsin.substr(0, found);
        }

    //read 4096 float-sized bytes
    if ((a = fread(feat, sizeof(*feat), imFeatureDim, f)) != imFeatureDim) {
      printf("Expected to read %d floats, got %d\n", imFeatureDim, a);
      exit(1);
    }

    if (itemIds.find(sAsin) == itemIds.end()) {
      continue;
    }

    vector<pair<int, float> > &vec = imageFeatures.at(itemIds[sAsin]);
    for (int f = 0; f < imFeatureDim; f ++) {
      if (feat[f] != 0) {  // compression
        vec.push_back(std::make_pair(f, feat[f]/ma));
      }
    }

    // print process
    counter ++;
    if (not (counter % 10000)) {
      fprintf(stderr, ".");
      fflush(stderr);
    }
  }
  fprintf(stderr, "\n");

  delete [] asin;
  delete [] feat;
  fclose(f);
}



